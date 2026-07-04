// dxImGuiRender.cpp — Metal backend for the engine's ImGui integration.
//
// The GL/DX11 targets compile Layers/xrRender/dxImGuiRender.cpp, which
// delegates to the official imgui backends (imgui_impl_opengl3 /
// imgui_impl_dx11).  The official Metal backend (imgui_impl_metal.mm) is
// Objective-C++, so this file reimplements its architecture in plain C++ on
// top of metal-cpp:
//   * dynamic texture management via ImGuiBackendFlags_RendererHasTextures
//     (ImTextureData WantCreate/WantUpdates/WantDestroy protocol, imgui 1.92+)
//   * a PSO cached per framebuffer pixel-format combination
//   * per-frame transient vertex/index buffers (ring of kFramesInFlight)
//
// Draw submission goes through the engine's render-pass manager
// (metalRenderPassManager) so ImGui renders into whatever targets the engine
// left bound — the backbuffer at UI time.
#include "stdafx.h"

#include "Layers/xrRender/dxImGuiRender.h"
#include "Layers/xrRenderMetal/metalHW.h"
#include "Layers/xrRenderMetal/metalRenderPassManager.h"

#include <imgui.h>

namespace xray::render::RENDER_NAMESPACE
{
namespace
{
// Must match the swapchain depth (see MetalConstantRing::kFramesInFlight) —
// a geometry slot is rewritten kFramesInFlight frames after it was encoded,
// when the GPU is guaranteed to have consumed it.
constexpr u32 kFramesInFlight = 3;

struct GeometrySlot
{
    MTL::Buffer* vb{};
    MTL::Buffer* ib{};
    u32 vbCapacity{};
    u32 ibCapacity{};
};

MTL::Library* s_library{};
MTL::Function* s_vertexFunction{};
MTL::Function* s_fragmentFunction{};
MTL::DepthStencilState* s_depthStencilState{};
// PSO per framebuffer format combination (hit rate ~100%: the UI pass always
// targets the swapchain)
xr_map<u64, MTL::RenderPipelineState*> s_pipelineCache;
GeometrySlot s_geometry[kFramesInFlight];
u32 s_frameIndex{};
bool s_deviceObjectsValid{};

// Same shader as the official imgui_impl_metal.mm backend
const char* const kShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
    float4x4 projectionMatrix;
};

struct VertexIn {
    float2 position  [[attribute(0)]];
    float2 texCoords [[attribute(1)]];
    uchar4 color     [[attribute(2)]];
};

struct VertexOut {
    float4 position [[position]];
    float2 texCoords;
    float4 color;
};

vertex VertexOut vertex_main(VertexIn in                 [[stage_in]],
                             constant Uniforms &uniforms [[buffer(1)]]) {
    VertexOut out;
    out.position = uniforms.projectionMatrix * float4(in.position, 0, 1);
    out.texCoords = in.texCoords;
    out.color = float4(in.color) / float4(255.0);
    return out;
}

fragment half4 fragment_main(VertexOut in [[stage_in]],
                             texture2d<half, access::sample> texture [[texture(0)]]) {
    constexpr sampler linearSampler(coord::normalized, min_filter::linear, mag_filter::linear, mip_filter::linear);
    half4 texColor = texture.sample(linearSampler, in.texCoords);
    return half4(in.color) * texColor;
}
)";

bool CreateDeviceObjects()
{
    if (s_deviceObjectsValid)
        return true;
    if (!HW.pDevice)
        return false;

    NS::Error* error = nullptr;
    s_library = HW.pDevice->newLibrary(NS::String::string(kShaderSource, NS::UTF8StringEncoding), nullptr, &error);
    if (!s_library)
    {
        Msg("! [Metal] ImGui: shader library creation failed: %s",
            (error && error->localizedDescription()) ? error->localizedDescription()->utf8String() : "unknown");
        return false;
    }
    s_vertexFunction = s_library->newFunction(NS::String::string("vertex_main", NS::UTF8StringEncoding));
    s_fragmentFunction = s_library->newFunction(NS::String::string("fragment_main", NS::UTF8StringEncoding));
    R_ASSERT(s_vertexFunction && s_fragmentFunction);

    // UI draws over everything: depth test always passes, no depth writes
    MTL::DepthStencilDescriptor* dsd = MTL::DepthStencilDescriptor::alloc()->init();
    dsd->setDepthWriteEnabled(false);
    dsd->setDepthCompareFunction(MTL::CompareFunctionAlways);
    s_depthStencilState = HW.pDevice->newDepthStencilState(dsd);
    dsd->release();

    s_deviceObjectsValid = true;
    return true;
}

void DestroyTexture(ImTextureData* tex)
{
    if (MTL::Texture* texture = reinterpret_cast<MTL::Texture*>((intptr_t)tex->TexID))
    {
        texture->release();
        tex->SetTexID(ImTextureID_Invalid);
        tex->BackendUserData = nullptr;
    }
    tex->SetStatus(ImTextureStatus_Destroyed);
}

void UpdateTexture(ImTextureData* tex)
{
    if (tex->Status == ImTextureStatus_WantCreate)
    {
        // Create and upload a new texture (dynamic font atlas page)
        IM_ASSERT(tex->TexID == ImTextureID_Invalid && tex->BackendUserData == nullptr);
        IM_ASSERT(tex->Format == ImTextureFormat_RGBA32);

        MTL::TextureDescriptor* desc = MTL::TextureDescriptor::texture2DDescriptor(
            MTL::PixelFormatRGBA8Unorm, NS::UInteger(tex->Width), NS::UInteger(tex->Height), false);
        desc->setUsage(MTL::TextureUsageShaderRead);
        desc->setStorageMode(MTL::StorageModeShared); // Apple Silicon UMA
        MTL::Texture* texture = HW.pDevice->newTexture(desc);
        if (!texture)
        {
            Msg("! [Metal] ImGui: texture creation failed (%dx%d)", tex->Width, tex->Height);
            return;
        }
        texture->replaceRegion(MTL::Region::Make2D(0, 0, NS::UInteger(tex->Width), NS::UInteger(tex->Height)), 0,
            tex->GetPixels(), NS::UInteger(tex->Width) * 4);

        tex->SetTexID((ImTextureID)(intptr_t)texture);
        tex->SetStatus(ImTextureStatus_OK);
    }
    else if (tex->Status == ImTextureStatus_WantUpdates)
    {
        MTL::Texture* texture = reinterpret_cast<MTL::Texture*>((intptr_t)tex->TexID);
        VERIFY(texture);
        for (ImTextureRect& r : tex->Updates)
        {
            texture->replaceRegion(MTL::Region::Make2D(NS::UInteger(r.x), NS::UInteger(r.y),
                NS::UInteger(r.w), NS::UInteger(r.h)), 0,
                tex->GetPixelsAt(r.x, r.y), NS::UInteger(tex->Width) * 4);
        }
        tex->SetStatus(ImTextureStatus_OK);
    }
    else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0)
    {
        DestroyTexture(tex);
    }
}

void DestroyDeviceObjects()
{
    // Destroy every texture this backend created
    if (ImGui::GetCurrentContext())
        for (ImTextureData* tex : ImGui::GetPlatformIO().Textures)
            if (tex->RefCount == 1)
                DestroyTexture(tex);

    for (auto& [key, pso] : s_pipelineCache)
        if (pso)
            pso->release();
    s_pipelineCache.clear();

    for (GeometrySlot& slot : s_geometry)
    {
        if (slot.vb)
            slot.vb->release();
        if (slot.ib)
            slot.ib->release();
        slot = {};
    }

    if (s_depthStencilState)
    {
        s_depthStencilState->release();
        s_depthStencilState = nullptr;
    }
    if (s_vertexFunction)
    {
        s_vertexFunction->release();
        s_vertexFunction = nullptr;
    }
    if (s_fragmentFunction)
    {
        s_fragmentFunction->release();
        s_fragmentFunction = nullptr;
    }
    if (s_library)
    {
        s_library->release();
        s_library = nullptr;
    }
    s_deviceObjectsValid = false;
}

MTL::RenderPipelineState* GetPipelineState(
    MTL::PixelFormat color, MTL::PixelFormat depth, MTL::PixelFormat stencil)
{
    const u64 key = (u64(color) << 32) | (u64(depth) << 16) | u64(stencil);
    const auto it = s_pipelineCache.find(key);
    if (it != s_pipelineCache.end())
        return it->second;

    // ImDrawVert layout at buffer 0; the Uniforms block binds at buffer 1
    MTL::VertexDescriptor* vertexDescriptor = MTL::VertexDescriptor::alloc()->init();
    vertexDescriptor->attributes()->object(0)->setOffset(offsetof(ImDrawVert, pos));
    vertexDescriptor->attributes()->object(0)->setFormat(MTL::VertexFormatFloat2);
    vertexDescriptor->attributes()->object(0)->setBufferIndex(0);
    vertexDescriptor->attributes()->object(1)->setOffset(offsetof(ImDrawVert, uv));
    vertexDescriptor->attributes()->object(1)->setFormat(MTL::VertexFormatFloat2);
    vertexDescriptor->attributes()->object(1)->setBufferIndex(0);
    vertexDescriptor->attributes()->object(2)->setOffset(offsetof(ImDrawVert, col));
    vertexDescriptor->attributes()->object(2)->setFormat(MTL::VertexFormatUChar4);
    vertexDescriptor->attributes()->object(2)->setBufferIndex(0);
    vertexDescriptor->layouts()->object(0)->setStepRate(1);
    vertexDescriptor->layouts()->object(0)->setStepFunction(MTL::VertexStepFunctionPerVertex);
    vertexDescriptor->layouts()->object(0)->setStride(sizeof(ImDrawVert));

    MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
    desc->setVertexFunction(s_vertexFunction);
    desc->setFragmentFunction(s_fragmentFunction);
    desc->setVertexDescriptor(vertexDescriptor);

    MTL::RenderPipelineColorAttachmentDescriptor* att = desc->colorAttachments()->object(0);
    att->setPixelFormat(color);
    att->setBlendingEnabled(true);
    att->setRgbBlendOperation(MTL::BlendOperationAdd);
    att->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
    att->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
    att->setAlphaBlendOperation(MTL::BlendOperationAdd);
    att->setSourceAlphaBlendFactor(MTL::BlendFactorOne);
    att->setDestinationAlphaBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);

    if (depth != MTL::PixelFormatInvalid)
        desc->setDepthAttachmentPixelFormat(depth);
    if (stencil != MTL::PixelFormatInvalid)
        desc->setStencilAttachmentPixelFormat(stencil);

    NS::Error* error = nullptr;
    MTL::RenderPipelineState* pso = HW.pDevice->newRenderPipelineState(desc, &error);
    desc->release();
    vertexDescriptor->release();

    if (!pso)
        Msg("! [Metal] ImGui: PSO creation failed: %s",
            (error && error->localizedDescription()) ? error->localizedDescription()->utf8String() : "unknown");

    s_pipelineCache.emplace(key, pso); // negative result cached — logged once
    return pso;
}

// Grow-only transient buffer (shared storage, CPU write-combined)
bool EnsureCapacity(MTL::Buffer*& buffer, u32& capacity, u32 required)
{
    if (buffer && capacity >= required)
        return true;
    if (buffer)
        buffer->release();
    capacity = required + required / 2;
    buffer = HW.pDevice->newBuffer(capacity,
        MTL::ResourceStorageModeShared | MTL::ResourceCPUCacheModeWriteCombined);
    if (!buffer)
    {
        Msg("! [Metal] ImGui: geometry buffer allocation failed (%u bytes)", capacity);
        capacity = 0;
        return false;
    }
    return true;
}
} // namespace

void dxImGuiRender::Copy(IImGuiRender& _in)
{
    *this = *dynamic_cast<dxImGuiRender*>(&_in);
}

void dxImGuiRender::SetState(ImDrawData* data)
{
    RCache.SetViewport({ 0.f, 0.f, data->DisplaySize.x, data->DisplaySize.y, 0.f, 1.f });
}

void dxImGuiRender::Frame()
{
    if (!s_deviceObjectsValid)
        CreateDeviceObjects();
}

void dxImGuiRender::Render(ImDrawData* data)
{
    const int fb_width = int(data->DisplaySize.x * data->FramebufferScale.x);
    const int fb_height = int(data->DisplaySize.y * data->FramebufferScale.y);
    if (fb_width <= 0 || fb_height <= 0)
        return;

    if (!CreateDeviceObjects())
        return;

    // Texture updates (dynamic font atlas). Most frames: 1 entry, status OK.
    if (data->Textures != nullptr)
        for (ImTextureData* tex : *data->Textures)
            if (tex->Status != ImTextureStatus_OK)
                UpdateTexture(tex);

    if (data->CmdLists.Size == 0 || data->TotalVtxCount <= 0 || data->TotalIdxCount <= 0)
        return;

    // Render into whatever targets the engine left bound (the backbuffer)
    MTL::RenderCommandEncoder* encoder = RPManager.EnsureEncoder();
    if (!encoder)
        return;

    MTL::RenderPipelineState* pso =
        GetPipelineState(RPManager.ColorFormat(0), RPManager.DepthFormat(), RPManager.StencilFormat());
    if (!pso)
        return;

    // Upload geometry into this frame's transient slot
    GeometrySlot& slot = s_geometry[s_frameIndex % kFramesInFlight];
    ++s_frameIndex;

    const u32 vertexBytes = u32(data->TotalVtxCount) * sizeof(ImDrawVert);
    const u32 indexBytes = u32(data->TotalIdxCount) * sizeof(ImDrawIdx);
    if (!EnsureCapacity(slot.vb, slot.vbCapacity, vertexBytes) ||
        !EnsureCapacity(slot.ib, slot.ibCapacity, indexBytes))
        return;

    // Render state
    encoder->setCullMode(MTL::CullModeNone);
    encoder->setDepthStencilState(s_depthStencilState);
    encoder->setRenderPipelineState(pso);
    encoder->setViewport({0.0, 0.0, double(fb_width), double(fb_height), 0.0, 1.0});

    // Orthographic projection over the ImGui display rect
    {
        const float L = data->DisplayPos.x;
        const float R = data->DisplayPos.x + data->DisplaySize.x;
        const float T = data->DisplayPos.y;
        const float B = data->DisplayPos.y + data->DisplaySize.y;
        const float N = 0.f, F = 1.f;
        const float ortho[4][4] =
        {
            { 2.0f/(R-L),   0.0f,           0.0f,   0.0f },
            { 0.0f,         2.0f/(T-B),     0.0f,   0.0f },
            { 0.0f,         0.0f,        1/(F-N),   0.0f },
            { (R+L)/(L-R),  (T+B)/(B-T), N/(F-N),   1.0f },
        };
        encoder->setVertexBytes(&ortho, sizeof(ortho), 1);
    }

    // Scissor projection offsets (multi-viewport/retina)
    const ImVec2 clip_off = data->DisplayPos;
    const ImVec2 clip_scale = data->FramebufferScale;

    u32 vertexOffset = 0;
    u32 indexOffset = 0;
    for (const ImDrawList* draw_list : data->CmdLists)
    {
        CopyMemory(static_cast<u8*>(slot.vb->contents()) + vertexOffset, draw_list->VtxBuffer.Data,
            size_t(draw_list->VtxBuffer.Size) * sizeof(ImDrawVert));
        CopyMemory(static_cast<u8*>(slot.ib->contents()) + indexOffset, draw_list->IdxBuffer.Data,
            size_t(draw_list->IdxBuffer.Size) * sizeof(ImDrawIdx));

        for (int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &draw_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback)
            {
                if (pcmd->UserCallback != ImDrawCallback_ResetRenderState)
                    pcmd->UserCallback(draw_list, pcmd);
                // ResetRenderState: nothing to do — state is re-set per draw
                continue;
            }

            // Project scissor rect into framebuffer space, clamp to bounds
            // (Metal validates setScissorRect against the attachment extents)
            float clip_min_x = (pcmd->ClipRect.x - clip_off.x) * clip_scale.x;
            float clip_min_y = (pcmd->ClipRect.y - clip_off.y) * clip_scale.y;
            float clip_max_x = (pcmd->ClipRect.z - clip_off.x) * clip_scale.x;
            float clip_max_y = (pcmd->ClipRect.w - clip_off.y) * clip_scale.y;
            clip_min_x = std::max(clip_min_x, 0.f);
            clip_min_y = std::max(clip_min_y, 0.f);
            clip_max_x = std::min(clip_max_x, float(fb_width));
            clip_max_y = std::min(clip_max_y, float(fb_height));
            if (clip_max_x <= clip_min_x || clip_max_y <= clip_min_y || pcmd->ElemCount == 0)
                continue;

            encoder->setScissorRect({NS::UInteger(clip_min_x), NS::UInteger(clip_min_y),
                NS::UInteger(clip_max_x - clip_min_x), NS::UInteger(clip_max_y - clip_min_y)});

            if (ImTextureID tex_id = pcmd->GetTexID())
                encoder->setFragmentTexture(reinterpret_cast<MTL::Texture*>((intptr_t)tex_id), 0);

            encoder->setVertexBuffer(slot.vb, vertexOffset + pcmd->VtxOffset * sizeof(ImDrawVert), 0);
            encoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(pcmd->ElemCount),
                sizeof(ImDrawIdx) == 2 ? MTL::IndexTypeUInt16 : MTL::IndexTypeUInt32, slot.ib,
                NS::UInteger(indexOffset) + pcmd->IdxOffset * sizeof(ImDrawIdx));
        }

        vertexOffset += u32(draw_list->VtxBuffer.Size) * sizeof(ImDrawVert);
        indexOffset += u32(draw_list->IdxBuffer.Size) * sizeof(ImDrawIdx);
    }

    // ImGui trampled the encoder's viewport/scissor: end the pass so the next
    // engine draw re-begins with the sticky state re-applied.
    metalRenderPass::InvalidatePass();
}

void dxImGuiRender::OnDeviceCreate(ImGuiContext* context)
{
    ImGui::SetAllocatorFunctions(
        [](size_t size, void* /*user_data*/)
        {
            return xr_malloc(size);
        },
        [](void* ptr, void* /*user_data*/)
        {
            xr_free(ptr);
        }
    );
    ImGui::SetCurrentContext(context);

    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = "xrRender_Metal";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    CreateDeviceObjects(); // retried lazily from Frame() if HW isn't ready yet
}

void dxImGuiRender::OnDeviceDestroy()
{
    DestroyDeviceObjects();

    if (ImGui::GetCurrentContext())
    {
        ImGuiIO& io = ImGui::GetIO();
        io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
    }
}

void dxImGuiRender::OnDeviceResetBegin()
{
    DestroyDeviceObjects();
}

void dxImGuiRender::OnDeviceResetEnd()
{
    CreateDeviceObjects();
}
} // namespace xray::render::RENDER_NAMESPACE
