#pragma once

// metalShaderCompiler.h — runtime GLSL -> MSL shader cross-compilation.
//
// Pipeline (SP2 plan, Task 16):
//   X-Ray GLSL (engine-preprocessed: option defines + resolved #includes, see
//   metal_shaders.cpp) -> glslang (SPIR-V, relaxed Vulkan rules) ->
//   SPIRV-Cross CompilerMSL (platform macOS, MSL 2.4) -> MTL::Library ->
//   MTL::Function.
//
// Binding model produced by this compiler (must stay in sync with the
// runtime contracts):
//   * Vertex attributes keep the layout(location = N) values of the GLSL
//     sources (COLOR=0, POSITION=3, TANGENT=4, NORMAL=5, BINORMAL=6, FOG=7,
//     TEXCOORD0..7=8..15 — see res/gamedata/shaders/gl/shared/common.h),
//     which SPIRV-Cross turns into [[attribute(N)]] — matching
//     MetalVertexInput::AttributeLocation (metalVertexInput.h).
//   * Default-uniform-block uniforms are packed by glslang into one uniform
//     block per stage (the "$Globals" equivalent) which is remapped to
//     [[buffer(METAL_CBUFFER_BIND_INDEX == 30)]] — matching
//     R_constants::flush_stage (metalr_constants.cpp).  Vertex streams stay
//     at [[buffer(0..)]] (METAL_VERTEX_STREAM_INDEX).
//   * Combined image samplers get SPIRV-Cross automatic [[texture(n)]] /
//     [[sampler(n)]] slots; the texture slot is reported through
//     MetalShaderReflection (offset field) for CTexture::apply (Task 13).
//
// NOTE: build-time compilation into .metallib bundles (engine preprocessor as
// a standalone tool + glslang + spirv-cross + `xcrun metal` at build time) is
// the production path per the SP2 design spec, but it is explicitly DEFERRED:
// the engine shader preprocessor (render options, skinning/MSAA permutations,
// include resolution against $game_shaders$) is deeply tied to runtime state
// (CRender::o, ps_r2_* console variables, mounted game archives) and cannot
// run at build time yet. Until it is extracted into a standalone tool, all
// shaders are cross-compiled at runtime through this class.
// LoadMetalLib() below is the designated entry point for the future
// pre-compiled path.

#include "Layers/xrRenderMetal/CommonTypes.h"
#include "Layers/xrRenderMetal/metalr_constants_cache.h" // MetalShaderReflection

#include <string>

namespace xray::render::RENDER_NAMESPACE
{
// Shader stage, decoupled from glslang's EShLanguage so that this header can
// be included without pulling glslang headers into every translation unit.
enum class MetalShaderStage : u32
{
    Vertex = 0,
    Fragment,
    Geometry, // no Metal equivalent — always yields a dead handle
    Hull,     // no direct Metal equivalent (MSL tessellation model differs)
    Domain,   // no direct Metal equivalent (MSL tessellation model differs)
    Compute,
};

// Result of a full shader compilation: the opaque handle stored in
// SVS/SPS/...::sh plus the reflection consumed by R_constant_table::parse.
// reflection.constants[i].name points into `names` — keep this object alive
// until parse() has copied the data (it copies into shared_str).
struct MetalCompiledShader
{
    u64 sh{}; // MetalShaderRegistry handle, 0 = failure / dead handle
    MetalShaderReflection reflection;
    xr_vector<xr_string> names; // backing storage for reflection name pcstrs
};

class MetalShaderCompiler
{
public:
    // Process-wide glslang initialization / teardown. Initialize() is called
    // lazily by CompileShader(), Destroy() on renderer shutdown.
    static bool Initialize();
    static void Destroy();

    // Cross-compile an engine-preprocessed GLSL shader given as an array of
    // null-terminated source chunks (option defines first, then the
    // include-expanded source — same layout the GL backend hands to
    // glShaderSource). On success fills `out` (handle + reflection) and
    // returns true. On failure logs the toolchain error and returns false.
    static bool CompileShader(pcstr name, pcstr const* sources, size_t sourceCount,
        MetalShaderStage stage, MetalCompiledShader& out);

    // GLSL -> MSL only (no Metal objects, usable from tools/tests).
    // Fills mslOut and the reflection stored in `out` (sh stays 0).
    static bool CompileGLSLToMSL(pcstr name, pcstr const* sources, size_t sourceCount,
        MetalShaderStage stage, std::string& mslOut, MetalCompiledShader& out,
        std::string& errorLog);

#if defined(USE_METAL)
    // Compile MSL source into a MTL::Library. Returns nullptr and fills
    // errorLog on failure. The caller owns the returned library (release()).
    static MTL::Library* CreateLibraryFromSource(MTL::Device* device,
        pcstr mslSource, std::string& errorLog);

    // Load a pre-compiled .metallib from disk (deferred build-time path).
    // Returns nullptr if the file is missing or invalid.
    static MTL::Library* LoadMetalLib(MTL::Device* device, pcstr path);

    // Extract a function by name. The caller owns the returned function.
    // SPIRV-Cross renames the GLSL entry point "main" to "main0".
    static MTL::Function* GetFunction(MTL::Library* library, pcstr name);
#endif
};

// ---------------------------------------------------------------------------
// MetalShaderRegistry — maps the opaque uint64_t shader handles used by the
// shared render code (SH_Atomic.h USE_METAL branches: SVS::sh, SPS::sh, ...)
// to the actual Metal objects.
//
// Handle 0 is reserved and means "null shader" (dead handle, e.g. the "null"
// pixel shader of depth-only passes, or geometry shaders which do not exist
// on Metal). Valid handles are (registry index + 1).
//
// Entries live for the renderer lifetime: SH_Atomic destructors only zero the
// handle (matching the placeholder contract there); the Metal objects are
// released in bulk by Clear() on device teardown. Shader counts are in the
// low thousands, so the leak-until-teardown policy is acceptable and mirrors
// the GL program cache behaviour closely enough.
//
// Not thread-safe: shader creation runs on the render thread only, like the
// GL backend's program creation.
// ---------------------------------------------------------------------------
class MetalShaderRegistry
{
public:
    struct Entry
    {
#if defined(USE_METAL)
        MTL::Function* function{}; // owned by the registry
        MTL::Library* library{};   // owned by the registry
#else
        void* function{};
        void* library{};
#endif
        MetalShaderStage stage{};
        xr_string name; // X-Ray shader name (debug / logging)
        std::string msl; // cross-compiled MSL source — kept for PSO debugging.
                         // TODO Task 20: drop once the pipeline is validated.
    };

    // Takes ownership of entry.function / entry.library.
    static u64 Register(Entry&& entry);

    static Entry* Get(u64 handle); // nullptr for 0 / out-of-range handles
#if defined(USE_METAL)
    static MTL::Function* Function(u64 handle); // nullptr for dead handles
#endif

    // Release all Metal objects (device teardown / vid_restart).
    static void Clear();
};

} // namespace xray::render::RENDER_NAMESPACE
