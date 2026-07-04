#include "stdafx.h"
#pragma hdrstop

#include "Layers/xrRenderMetal/metalShaderCompiler.h"
#include "Layers/xrRenderMetal/metalConstantBuffer.h" // METAL_CBUFFER_BIND_INDEX
#include "Layers/xrRender/r_constants.h" // RC_* enums

// SPIRV-Cross first: its spirv.hpp and glslang's SPIRV/spirv.hpp share the
// same Khronos include guard (spirv_HPP), so whichever comes first wins.
// Both carry every core enum this file uses.
#include <spirv_msl.hpp>

#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>

namespace xray::render::RENDER_NAMESPACE
{
// ---------------------------------------------------------------------------
// glslang gathers all default-uniform-block uniforms ("$Globals" in HLSL
// terms) into one uniform block per stage at this descriptor set / binding.
// Set 1 keeps it away from the combined image samplers, which are auto-mapped
// into set 0.  SPIRV-Cross then remaps the block to MSL buffer index
// METAL_CBUFFER_BIND_INDEX (30) — see AddGlobalsBufferRemap below.
// ---------------------------------------------------------------------------
static constexpr unsigned int kGlobalsDescSet = 1;
static constexpr unsigned int kGlobalsBinding = 0;
static constexpr cpcstr kGlobalsBlockName = "_Globals";

// ===========================================================================
// MetalShaderRegistry
// ===========================================================================
// Not thread-safe by design — shader creation is render-thread-only, like the
// GL backend's program creation.
static xr_vector<MetalShaderRegistry::Entry> s_registry;

u64 MetalShaderRegistry::Register(Entry&& entry)
{
    s_registry.emplace_back(std::move(entry));
    return u64(s_registry.size()); // handle == index + 1, 0 is reserved
}

MetalShaderRegistry::Entry* MetalShaderRegistry::Get(u64 handle)
{
    if (0 == handle || handle > s_registry.size())
        return nullptr;
    return &s_registry[size_t(handle - 1)];
}

#if defined(USE_METAL)
MTL::Function* MetalShaderRegistry::Function(u64 handle)
{
    Entry* entry = Get(handle);
    return entry ? entry->function : nullptr;
}
#endif

void MetalShaderRegistry::Clear()
{
#if defined(USE_METAL)
    for (Entry& entry : s_registry)
    {
        if (entry.function)
            entry.function->release();
        if (entry.library)
            entry.library->release();
    }
#endif
    s_registry.clear();
}

// ===========================================================================
// glslang: engine-preprocessed GLSL -> SPIR-V
// ===========================================================================
static bool s_glslang_initialized = false;

bool MetalShaderCompiler::Initialize()
{
    if (s_glslang_initialized)
        return true;
    s_glslang_initialized = glslang::InitializeProcess();
    if (!s_glslang_initialized)
        Msg("! Metal: glslang::InitializeProcess failed");
    return s_glslang_initialized;
}

void MetalShaderCompiler::Destroy()
{
    MetalShaderRegistry::Clear();
    if (s_glslang_initialized)
    {
        glslang::FinalizeProcess();
        s_glslang_initialized = false;
    }
}

static EShLanguage to_glslang_stage(MetalShaderStage stage)
{
    switch (stage)
    {
    case MetalShaderStage::Vertex: return EShLangVertex;
    case MetalShaderStage::Fragment: return EShLangFragment;
    case MetalShaderStage::Geometry: return EShLangGeometry;
    case MetalShaderStage::Hull: return EShLangTessControl;
    case MetalShaderStage::Domain: return EShLangTessEvaluation;
    case MetalShaderStage::Compute: return EShLangCompute;
    default: NODEFAULT;
    }
    return EShLangVertex;
}

static spv::ExecutionModel to_execution_model(MetalShaderStage stage)
{
    switch (stage)
    {
    case MetalShaderStage::Vertex: return spv::ExecutionModelVertex;
    case MetalShaderStage::Fragment: return spv::ExecutionModelFragment;
    case MetalShaderStage::Compute: return spv::ExecutionModelGLCompute;
    default: NODEFAULT;
    }
    return spv::ExecutionModelVertex;
}

static bool compile_glsl_to_spirv(pcstr name, pcstr const* sources, size_t sourceCount,
    MetalShaderStage stage, std::vector<unsigned int>& spirvOut, std::string& errorLog)
{
    const EShLanguage lang = to_glslang_stage(stage);
    glslang::TShader shader(lang);
    shader.setStrings(sources, int(sourceCount));

    // The X-Ray GLSL sources are OpenGL-flavoured (default-uniform-block
    // uniforms, combined samplers, no explicit bindings).  Relaxed Vulkan
    // rules make glslang accept them and gather the loose uniforms into one
    // uniform block; auto-mapping assigns the bindings/locations the sources
    // do not declare (the vertex attributes and varyings all carry explicit
    // layout(location = N), which is preserved).
    shader.setEnvInput(glslang::EShSourceGlsl, lang, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
    shader.setEnvInputVulkanRulesRelaxed();
    shader.setGlobalUniformBlockName(kGlobalsBlockName);
    shader.setGlobalUniformSet(kGlobalsDescSet);
    shader.setGlobalUniformBinding(kGlobalsBinding);
    shader.setAutoMapBindings(true);
    shader.setAutoMapLocations(true);

    const EShMessages messages = EShMessages(EShMsgSpvRules | EShMsgVulkanRules);

    if (!shader.parse(GetDefaultResources(), 410, false, messages))
    {
        errorLog = shader.getInfoLog();
        return false;
    }

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages))
    {
        errorLog = program.getInfoLog();
        return false;
    }
    if (!program.mapIO()) // applies the auto-mapped bindings/locations
    {
        errorLog = program.getInfoLog();
        return false;
    }

    glslang::SpvOptions options;
    options.disableOptimizer = true;
    options.stripDebugInfo = false; // reflection needs OpName/OpMemberName
    options.validate = false;

    spv::SpvBuildLogger logger;
    glslang::GlslangToSpv(*program.getIntermediate(lang), spirvOut, &logger, &options);

    const std::string spvMessages = logger.getAllMessages();
    if (!spvMessages.empty())
        Msg("~ Metal: SPIR-V generation for '%s': %s", name, spvMessages.c_str());

    return true;
}

// ===========================================================================
// SPIRV-Cross: SPIR-V -> MSL + reflection
// ===========================================================================

// GL binds the fragment outputs by NAME (glBindFragDataLocation "SV_Target",
// "SV_Target0".."SV_Target2") and the GLSL sources declare them without
// explicit locations.  Re-apply the same convention to the SPIR-V location
// decorations so SPIRV-Cross emits the matching [[color(N)]] attributes.
static void remap_fragment_outputs(spirv_cross::CompilerMSL& msl,
    const spirv_cross::ShaderResources& resources)
{
    for (const spirv_cross::Resource& output : resources.stage_outputs)
    {
        static const char prefix[] = "SV_Target";
        constexpr size_t prefix_len = sizeof(prefix) - 1;
        if (0 != output.name.compare(0, prefix_len, prefix))
            continue;

        u32 location = 0; // plain "SV_Target" aliases "SV_Target0"
        if (output.name.length() > prefix_len)
            location = u32(atoi(output.name.c_str() + prefix_len));
        msl.set_decoration(output.id, spv::DecorationLocation, location);
    }
}

// Map a $Globals member type to the engine's R_constant classification.
static bool map_member_class(const spirv_cross::SPIRType& type, pcstr blockName,
    pcstr memberName, u16& rcType, u16& rcClass)
{
    switch (type.basetype)
    {
    case spirv_cross::SPIRType::Float: rcType = RC_float; break;
    case spirv_cross::SPIRType::Int:
    case spirv_cross::SPIRType::UInt: rcType = RC_int; break;
    case spirv_cross::SPIRType::Boolean: rcType = RC_bool; break;
    default:
        Msg("! Metal: unsupported uniform base type for '%s.%s'", blockName, memberName);
        return false;
    }

    const bool isArray = !type.array.empty();
    if (type.columns == 1)
    {
        switch (type.vecsize)
        {
        case 1: rcClass = RC_1x1; break;
        case 2: rcClass = RC_1x2; break;
        case 3: rcClass = RC_1x3; break;
        case 4: rcClass = isArray ? RC_1x4a : RC_1x4; break;
        default:
            Msg("! Metal: unsupported vector size for '%s.%s'", blockName, memberName);
            return false;
        }
    }
    else if (type.columns == 4) // GLSL matNx4 family: mat4, mat4x3, mat4x2
    {
        switch (type.vecsize)
        {
        case 2: rcClass = RC_2x4; break;
        case 3: rcClass = isArray ? RC_3x4a : RC_3x4; break;
        case 4: rcClass = isArray ? RC_4x4a : RC_4x4; break;
        default:
            Msg("! Metal: unsupported matrix size for '%s.%s'", blockName, memberName);
            return false;
        }
    }
    else
    {
        // mat2 / mat3 uniforms — the GL backend fatals on these too.
        Msg("! Metal: unsupported matrix dimensions for '%s.%s'", blockName, memberName);
        return false;
    }
    return true;
}

bool MetalShaderCompiler::CompileGLSLToMSL(pcstr name, pcstr const* sources,
    size_t sourceCount, MetalShaderStage stage, std::string& mslOut,
    MetalCompiledShader& out, std::string& errorLog)
{
    out.reflection.blockSize = 0;
    out.reflection.constants.clear();
    out.names.clear();

    std::vector<unsigned int> spirv;
    if (!compile_glsl_to_spirv(name, sources, sourceCount, stage, spirv, errorLog))
        return false;

    // SPIRV-Cross reports malformed input through exceptions.  They are
    // caught right here, entirely below any LuaJIT frame, so this does not
    // violate the XRAY_EXCEPTIONS=0 constraint (which only forbids exceptions
    // crossing engine/LuaJIT stack frames).
    try
    {
        spirv_cross::CompilerMSL msl(std::move(spirv));

        spirv_cross::CompilerMSL::Options options;
        options.platform = spirv_cross::CompilerMSL::Options::macOS;
        options.set_msl_version(2, 4); // macOS 12+, well below our 15.0 target
        msl.set_msl_options(options);

        const spirv_cross::ShaderResources resources = msl.get_shader_resources();

        if (stage == MetalShaderStage::Fragment)
            remap_fragment_outputs(msl, resources);

        // Locate the glslang-generated globals block (there is at most one;
        // the X-Ray GLSL sources declare no explicit uniform blocks).
        const spirv_cross::Resource* globals = nullptr;
        for (const spirv_cross::Resource& ubo : resources.uniform_buffers)
        {
            if (msl.get_decoration(ubo.id, spv::DecorationDescriptorSet) == kGlobalsDescSet &&
                msl.get_decoration(ubo.id, spv::DecorationBinding) == kGlobalsBinding)
            {
                globals = &ubo;
            }
            else
            {
                Msg("~ Metal: '%s' declares unexpected uniform block '%s'", name, ubo.name.c_str());
            }
        }

        if (globals)
        {
            // Pin the block to [[buffer(METAL_CBUFFER_BIND_INDEX)]].  Without
            // the remap SPIRV-Cross would auto-assign buffer index 0, which
            // collides with the vertex stream (METAL_VERTEX_STREAM_INDEX).
            spirv_cross::MSLResourceBinding remap;
            remap.stage = to_execution_model(stage);
            remap.desc_set = kGlobalsDescSet;
            remap.binding = kGlobalsBinding;
            remap.count = 1;
            remap.msl_buffer = METAL_CBUFFER_BIND_INDEX;
            msl.add_msl_resource_binding(remap);

            // Flip the matrix members to row-major storage.  R_constants
            // writes matrices in the DX11 register layout — one float4 line
            // per matrix row of the transposed Fmatrix (see
            // metalr_constants_cache.h).  That byte layout equals RowMajor
            // std140 here, NOT the ColMajor layout glslang emits for GLSL
            // matrices (which would transpose every transform and pad
            // mat4x3 to four columns).  The GL backend achieves the same
            // effect by uploading with transpose == GL_TRUE.
            // TODO Task 20: validate visually — this is the highest-risk
            // convention in the shader pipeline.
            const spirv_cross::SPIRType& blockType = msl.get_type(globals->base_type_id);
            for (u32 i = 0; i < u32(blockType.member_types.size()); ++i)
            {
                const spirv_cross::SPIRType& memberType = msl.get_type(blockType.member_types[i]);
                if (memberType.columns > 1)
                {
                    msl.unset_member_decoration(blockType.self, i, spv::DecorationColMajor);
                    msl.set_member_decoration(blockType.self, i, spv::DecorationRowMajor);
                }
            }
        }

        mslOut = msl.compile();

        // -------------------------------------------------------------------
        // Reflection (consumed by R_constant_table::parse, metalr_constants.cpp)
        // -------------------------------------------------------------------
        struct member_info
        {
            u16 type;
            u16 cls;
            u32 offset;
        };
        xr_vector<member_info> infos;

        if (globals)
        {
            const spirv_cross::SPIRType& blockType = msl.get_type(globals->base_type_id);
            out.reflection.blockSize = u32(msl.get_declared_struct_size(blockType));

            for (u32 i = 0; i < u32(blockType.member_types.size()); ++i)
            {
                const spirv_cross::SPIRType& memberType = msl.get_type(blockType.member_types[i]);
                const std::string& memberName = msl.get_member_name(blockType.self, i);

                u16 rcType, rcClass;
                if (!map_member_class(memberType, kGlobalsBlockName, memberName.c_str(), rcType, rcClass))
                {
                    errorLog = "unsupported uniform type in " + std::string(kGlobalsBlockName) +
                        "." + memberName;
                    return false;
                }

                out.names.emplace_back(memberName.c_str());
                infos.emplace_back(member_info{ rcType, rcClass,
                    msl.type_struct_member_offset(blockType, i) });
            }
        }

        // Samplers: report the automatically assigned [[texture(n)]] slot
        // (only known after compile()) through the offset field.
        for (const spirv_cross::Resource& sampler : resources.sampled_images)
        {
            const u32 slot = msl.get_automatic_msl_resource_binding(sampler.id);
            if (slot == u32(-1))
            {
                Msg("~ Metal: sampler '%s' in '%s' got no texture slot (unused?)", sampler.name.c_str(), name);
                continue;
            }
            out.names.emplace_back(sampler.name.c_str());
            infos.emplace_back(member_info{ RC_sampler, RC_sampler, slot });
        }

        // Names are fully collected — xr_vector will not reallocate anymore,
        // so the pcstr views handed to parse() stay valid.
        out.reflection.constants.reserve(infos.size());
        for (size_t i = 0; i < infos.size(); ++i)
        {
            MetalConstantReflection rc;
            rc.name = out.names[i].c_str();
            rc.type = infos[i].type;
            rc.cls = infos[i].cls;
            rc.offset = infos[i].offset;
            out.reflection.constants.emplace_back(rc);
        }
    }
    catch (const spirv_cross::CompilerError& e)
    {
        errorLog = e.what();
        return false;
    }

    return true;
}

// ===========================================================================
// Metal object creation
// ===========================================================================
#if defined(USE_METAL)
MTL::Library* MetalShaderCompiler::CreateLibraryFromSource(MTL::Device* device,
    pcstr mslSource, std::string& errorLog)
{
    VERIFY(device);
    NS::String* source = NS::String::alloc()->init(mslSource, NS::UTF8StringEncoding);
    MTL::CompileOptions* options = MTL::CompileOptions::alloc()->init();
    options->setLanguageVersion(MTL::LanguageVersion2_4);

    NS::Error* error = nullptr;
    MTL::Library* library = device->newLibrary(source, options, &error);
    options->release();
    source->release();

    if (!library && error)
        errorLog = error->localizedDescription()->utf8String();
    return library;
}

MTL::Library* MetalShaderCompiler::LoadMetalLib(MTL::Device* device, pcstr path)
{
    VERIFY(device);
    NS::String* filePath = NS::String::alloc()->init(path, NS::UTF8StringEncoding);
    NS::Error* error = nullptr;
    MTL::Library* library = device->newLibrary(filePath, &error);
    filePath->release();

    if (!library && error)
        Msg("! Metal: failed to load metallib '%s': %s", path, error->localizedDescription()->utf8String());
    return library;
}

MTL::Function* MetalShaderCompiler::GetFunction(MTL::Library* library, pcstr name)
{
    VERIFY(library);
    NS::String* functionName = NS::String::alloc()->init(name, NS::UTF8StringEncoding);
    MTL::Function* function = library->newFunction(functionName);
    functionName->release();
    return function;
}
#endif // USE_METAL

// ===========================================================================
// Full runtime pipeline
// ===========================================================================
bool MetalShaderCompiler::CompileShader(pcstr name, pcstr const* sources,
    size_t sourceCount, MetalShaderStage stage, MetalCompiledShader& out)
{
    out.sh = 0;

    if (!Initialize())
        return false;

    std::string msl, errorLog;
    if (!CompileGLSLToMSL(name, sources, sourceCount, stage, msl, out, errorLog))
    {
        Log("! Metal shader cross-compilation failed:", name);
        Log("! error:", errorLog.c_str());
        return false;
    }

#if defined(USE_METAL)
    MTL::Library* library = CreateLibraryFromSource(HW.pDevice, msl.c_str(), errorLog);
    if (!library)
    {
        Log("! Metal shader library creation failed:", name);
        Log("! error:", errorLog.c_str());
        Log("! MSL source:");
        Log(msl.c_str());
        return false;
    }

    // SPIRV-Cross renames the GLSL entry point "main" to "main0".
    MTL::Function* function = GetFunction(library, "main0");
    if (!function)
    {
        Log("! Metal shader has no entry point 'main0':", name);
        library->release();
        return false;
    }

    MetalShaderRegistry::Entry entry;
    entry.function = function;
    entry.library = library;
    entry.stage = stage;
    entry.name = name;
    entry.msl = std::move(msl);
    out.sh = MetalShaderRegistry::Register(std::move(entry));
#endif // USE_METAL

    return true;
}
} // namespace xray::render::RENDER_NAMESPACE
