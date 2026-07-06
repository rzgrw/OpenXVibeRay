// metalRenderPassManager.cpp — MTL::RenderPassDescriptor / RenderCommandEncoder
// lifecycle plus the metalPipeline PSO cache. See metalRenderPassManager.h and
// metalR_Backend_Runtime.h for the model.
#include "stdafx.h"
#pragma hdrstop

#include "Layers/xrRenderMetal/metalRenderPassManager.h"
#include "Layers/xrRenderMetal/metalHW.h"
#include "Layers/xrRenderMetal/metalConstantBuffer.h"
#include "Layers/xrRenderMetal/metalState.h"
#include "Layers/xrRenderMetal/metalStateUtils.h"
#include "Layers/xrRenderMetal/metalVertexInput.h"
#include "Layers/xrRenderMetal/metalShaderCompiler.h" // MetalShaderRegistry::Function

#if defined(USE_METAL)

namespace xray::render::RENDER_NAMESPACE
{
metalRenderPassManager RPManager;

static bool has_stencil(MTL::PixelFormat fmt)
{
    // Apple Silicon has no 24-bit depth; Depth32Float_Stencil8 is the packed
    // format the backend uses for D24S8 requests.
    return fmt == MTL::PixelFormatDepth32Float_Stencil8 || fmt == MTL::PixelFormatStencil8 ||
        fmt == MTL::PixelFormatX32_Stencil8;
}

//-----------------------------------------------------------------------------
// Device lifecycle
//-----------------------------------------------------------------------------
void metalRenderPassManager::OnDeviceCreate()
{
    m_dirty = true;
}

void metalRenderPassManager::OnDeviceDestroy()
{
    EndEncoder();
    metalPipeline::OnDeviceDestroy();
    m_visibilityBuffer = nullptr;
    for (u32 i = 0; i < MaxColorTargets; ++i)
        m_rt[i] = 0;
    m_zb = 0;
    m_pendingColor.clear();
    m_pendingDepth.clear();
    m_viewportValid = false;
    m_scissorEnabled = false;
    m_dirty = true;
}

//-----------------------------------------------------------------------------
// Frame lifecycle (called from CHW::BeginScene / CHW::EndScene)
//-----------------------------------------------------------------------------
void metalRenderPassManager::OnFrameBegin()
{
    // Recycle the transient constant-buffer slices (exactly once per frame —
    // see metalConstantBuffer.h).
    g_ConstantRing.OnFrameBegin();

    // New drawable: even an unchanged handle set must not reuse pass state
    // captured against the previous frame's backbuffer texture.
    m_dirty = true;
}

void metalRenderPassManager::OnFrameEnd()
{
    EndEncoder();
    FlushPendingClears();
}

//-----------------------------------------------------------------------------
// metalRenderPass contract — state recording
//-----------------------------------------------------------------------------
void metalRenderPassManager::InvalidatePass()
{
    // Attachment set changed: the open encoder (if any) is stale.
    EndEncoder(); // sets m_dirty
    m_dirty = true;
}

void metalRenderPassManager::RequestClearColor(uint64_t rt, const Fcolor& color)
{
    VERIFY(rt); // handle 0 (backbuffer/none) is filtered by CBackend::ClearRT
    if (!rt)
        return;

    // Record (or overwrite) the pending clear for this attachment.
    bool found = false;
    for (PendingColorClear& pc : m_pendingColor)
    {
        if (pc.rt == rt)
        {
            pc.color = color;
            found = true;
            break;
        }
    }
    if (!found)
        m_pendingColor.push_back({rt, color});

    // GL/DX clears are imperative: if a pass with this attachment bound is
    // open, restart it so the clear lands exactly where it was requested.
    if (m_encoder)
    {
        for (u32 i = 0; i < MaxColorTargets; ++i)
        {
            if (m_rt[i] == rt)
            {
                EndEncoder();
                break;
            }
        }
    }
}

void metalRenderPassManager::RequestClearDepth(uint64_t zb, float depth)
{
    VERIFY(zb); // handle 0 is filtered by CBackend::ClearZB
    if (!zb)
        return;

    bool found = false;
    for (PendingDepthClear& pd : m_pendingDepth)
    {
        if (pd.zb == zb)
        {
            pd.depth = true;
            pd.depthValue = depth;
            found = true;
            break;
        }
    }
    if (!found)
        m_pendingDepth.push_back({zb, true, depth, false, 0});

    if (m_encoder && m_zb == zb)
        EndEncoder();
}

void metalRenderPassManager::RequestClearStencil(uint64_t zb, u8 stencil)
{
    VERIFY(zb);
    if (!zb)
        return;

    bool found = false;
    for (PendingDepthClear& pd : m_pendingDepth)
    {
        if (pd.zb == zb)
        {
            pd.stencil = true;
            pd.stencilValue = stencil;
            found = true;
            break;
        }
    }
    if (!found)
        m_pendingDepth.push_back({zb, false, 1.f, true, stencil});

    if (m_encoder && m_zb == zb)
        EndEncoder();
}

void metalRenderPassManager::SetViewport(const D3D_VIEWPORT& viewport)
{
    m_viewport = viewport; // slice to the plain XR_METAL_VIEWPORT storage
    m_viewportValid = true;
    if (m_encoder)
        ApplyViewportAndScissor();
}

void metalRenderPassManager::SetScissor(const Irect* rect)
{
    if (rect)
    {
        m_scissor = *rect;
        m_scissorEnabled = true;
    }
    else
    {
        m_scissorEnabled = false; // full-target rect on apply
    }
    if (m_encoder)
        ApplyViewportAndScissor();
}

//-----------------------------------------------------------------------------
// Attachment resolution
//-----------------------------------------------------------------------------
MTL::Texture* metalRenderPassManager::ColorTexture(u32 index) const
{
    VERIFY(index < MaxColorTargets);
    if (m_rt[index])
        return reinterpret_cast<MTL::Texture*>(m_rt[index]);
    // Handle 0 on slot 0 addresses the backbuffer (current drawable)
    if (index == 0 && HW.pCurrentDrawable)
        return HW.pCurrentDrawable->texture();
    return nullptr;
}

MTL::Texture* metalRenderPassManager::DepthTexture() const
{
    // No default depth buffer: the base zb is created by CRenderTarget
    // (metalSH_RT) and bound explicitly through set_ZB.
    return reinterpret_cast<MTL::Texture*>(m_zb);
}

MTL::PixelFormat metalRenderPassManager::ColorFormat(u32 index) const
{
    if (MTL::Texture* texture = ColorTexture(index))
        return texture->pixelFormat();
    // Fall back to the swapchain format so PSOs created between frames
    // (when no drawable is held) still match the backbuffer.
    if (index == 0 && HW.pMetalLayer)
        return HW.pMetalLayer->pixelFormat();
    return MTL::PixelFormatInvalid;
}

MTL::PixelFormat metalRenderPassManager::DepthFormat() const
{
    if (MTL::Texture* texture = DepthTexture())
        return texture->pixelFormat();
    return MTL::PixelFormatInvalid;
}

MTL::PixelFormat metalRenderPassManager::StencilFormat() const
{
    const MTL::PixelFormat fmt = DepthFormat();
    return has_stencil(fmt) ? fmt : MTL::PixelFormatInvalid;
}

void metalRenderPassManager::SetVisibilityBuffer(MTL::Buffer* buffer)
{
    if (m_visibilityBuffer == buffer)
        return;
    m_visibilityBuffer = buffer;
    m_dirty = true; // must be baked into the next render pass descriptor
}

//-----------------------------------------------------------------------------
// Encoder lifecycle
//-----------------------------------------------------------------------------
MTL::RenderCommandEncoder* metalRenderPassManager::EnsureEncoder(
    const uint64_t (&rt)[MaxColorTargets], uint64_t zb)
{
    // set_RT/set_ZB already call InvalidatePass on change; syncing here is the
    // safety net for callers that bypass the CBackend recording.
    for (u32 i = 0; i < MaxColorTargets; ++i)
    {
        if (m_rt[i] != rt[i])
        {
            m_rt[i] = rt[i];
            m_dirty = true;
        }
    }
    if (m_zb != zb)
    {
        m_zb = zb;
        m_dirty = true;
    }
    return EnsureEncoder();
}

MTL::RenderCommandEncoder* metalRenderPassManager::EnsureEncoder()
{
    if (m_encoder && !m_dirty)
        return m_encoder;

    EndEncoder();

    if (!HW.pCurrentCommandBuffer)
        return nullptr; // outside BeginScene/EndScene — draw is dropped

    MTL::RenderPassDescriptor* desc = MTL::RenderPassDescriptor::alloc()->init();

    bool haveAttachment = false;
    u32 width = 0, height = 0;

    for (u32 i = 0; i < MaxColorTargets; ++i)
    {
        MTL::Texture* texture = ColorTexture(i);
        if (!texture)
            continue;

        MTL::RenderPassColorAttachmentDescriptor* att = desc->colorAttachments()->object(i);
        att->setTexture(texture);
        att->setStoreAction(MTL::StoreActionStore);
        att->setLoadAction(MTL::LoadActionLoad);

        // Consume the pending clear recorded for this handle, if any
        // (handle 0 == backbuffer never has one — see header note).
        if (m_rt[i])
        {
            for (auto it = m_pendingColor.begin(); it != m_pendingColor.end(); ++it)
            {
                if (it->rt != m_rt[i])
                    continue;
                att->setLoadAction(MTL::LoadActionClear);
                att->setClearColor(MTL::ClearColor::Make(it->color.r, it->color.g, it->color.b, it->color.a));
                m_pendingColor.erase(it);
                break;
            }
        }

        haveAttachment = true;
        if (!width)
        {
            width = (u32)texture->width();
            height = (u32)texture->height();
        }
    }

    if (MTL::Texture* depth = DepthTexture())
    {
        bool clearDepth = false, clearStencil = false;
        float depthValue = 1.f;
        u8 stencilValue = 0;
        for (auto it = m_pendingDepth.begin(); it != m_pendingDepth.end(); ++it)
        {
            if (it->zb != m_zb)
                continue;
            clearDepth = it->depth;
            depthValue = it->depthValue;
            clearStencil = it->stencil;
            stencilValue = it->stencilValue;
            m_pendingDepth.erase(it);
            break;
        }

        MTL::RenderPassDepthAttachmentDescriptor* att = desc->depthAttachment();
        att->setTexture(depth);
        att->setStoreAction(MTL::StoreActionStore);
        att->setLoadAction(clearDepth ? MTL::LoadActionClear : MTL::LoadActionLoad);
        att->setClearDepth(depthValue);

        if (has_stencil(depth->pixelFormat()))
        {
            MTL::RenderPassStencilAttachmentDescriptor* satt = desc->stencilAttachment();
            satt->setTexture(depth);
            satt->setStoreAction(MTL::StoreActionStore);
            satt->setLoadAction(clearStencil ? MTL::LoadActionClear : MTL::LoadActionLoad);
            satt->setClearStencil(stencilValue);
        }

        haveAttachment = true;
        if (!width)
        {
            width = (u32)depth->width();
            height = (u32)depth->height();
        }
    }

    if (!haveAttachment)
    {
        desc->release();
        return nullptr; // nothing to render into
    }

    // Metal requires the visibility buffer at pass creation (occlusion queries)
    if (m_visibilityBuffer)
        desc->setVisibilityResultBuffer(m_visibilityBuffer);

    m_encoder = HW.pCurrentCommandBuffer->renderCommandEncoder(desc);
    desc->release();

    if (m_encoder)
    {
        m_encoder->retain();
        m_passWidth = width;
        m_passHeight = height;
        // Publish the active encoder so per-draw resource binding (textures at
        // CBackend::Render, and metal_bind_texture's other callers) can reach it.
        HW.pCurrentRenderEncoder = m_encoder;
        ApplyViewportAndScissor(); // sticky state, re-applied per new encoder
        m_dirty = false;
    }
    else
    {
        Msg("! [Metal] renderCommandEncoder creation failed");
    }
    return m_encoder;
}

void metalRenderPassManager::EndEncoder()
{
    if (!m_encoder)
        return;
    m_encoder->endEncoding();
    m_encoder->release();
    m_encoder = nullptr;
    HW.pCurrentRenderEncoder = nullptr;
    m_passWidth = m_passHeight = 0;
    m_dirty = true;
}

//-----------------------------------------------------------------------------
// Sticky viewport / scissor
//-----------------------------------------------------------------------------
void metalRenderPassManager::ApplyViewportAndScissor()
{
    VERIFY(m_encoder);
    const double W = double(m_passWidth);
    const double H = double(m_passHeight);

    // Viewport — clamped to the attachment extents (Metal validation errors
    // on out-of-bounds rects).  Top-left origin like D3D: no Y-flip.
    MTL::Viewport viewport = {0.0, 0.0, W, H, 0.0, 1.0};
    if (m_viewportValid)
    {
        const double x = std::min(std::max(double(m_viewport.TopLeftX), 0.0), W);
        const double y = std::min(std::max(double(m_viewport.TopLeftY), 0.0), H);
        const double w = std::min(std::max(double(m_viewport.Width), 0.0), W - x);
        const double h = std::min(std::max(double(m_viewport.Height), 0.0), H - y);
        viewport = {x, y, w, h, double(m_viewport.MinDepth), double(m_viewport.MaxDepth)};
    }
    m_encoder->setViewport(viewport);

    // Scissor — disabled means a full-target rect (Metal has no toggle).
    MTL::ScissorRect scissor = {0, 0, m_passWidth, m_passHeight};
    if (m_scissorEnabled)
    {
        const u32 x0 = std::min(u32(std::max(m_scissor.left, 0)), m_passWidth);
        const u32 y0 = std::min(u32(std::max(m_scissor.top, 0)), m_passHeight);
        const u32 x1 = std::min(u32(std::max(m_scissor.right, 0)), m_passWidth);
        const u32 y1 = std::min(u32(std::max(m_scissor.bottom, 0)), m_passHeight);
        scissor = {x0, y0, x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0};
    }
    m_encoder->setScissorRect(scissor);
}

//-----------------------------------------------------------------------------
// Pending-clear flush (frame end) and immediate clear-only passes
//-----------------------------------------------------------------------------
void metalRenderPassManager::FlushPendingClears()
{
    if (m_pendingColor.empty() && m_pendingDepth.empty())
        return;

    if (!HW.pCurrentCommandBuffer)
    {
        m_pendingColor.clear();
        m_pendingDepth.clear();
        return;
    }

    for (const PendingColorClear& pc : m_pendingColor)
        if (MTL::Texture* texture = reinterpret_cast<MTL::Texture*>(pc.rt))
            ClearImmediateColor(texture, pc.color);
    m_pendingColor.clear();

    for (const PendingDepthClear& pd : m_pendingDepth)
        if (MTL::Texture* texture = reinterpret_cast<MTL::Texture*>(pd.zb))
            ClearImmediateDepth(texture, pd.depth, pd.depthValue, pd.stencil, pd.stencilValue);
    m_pendingDepth.clear();
}

void metalRenderPassManager::ClearImmediateColor(MTL::Texture* texture, const Fcolor& color)
{
    if (!HW.pCurrentCommandBuffer)
        return;

    // Command-buffer encoders must never overlap.
    EndEncoder();

    MTL::RenderPassDescriptor* desc = MTL::RenderPassDescriptor::alloc()->init();
    MTL::RenderPassColorAttachmentDescriptor* att = desc->colorAttachments()->object(0);
    att->setTexture(texture);
    att->setLoadAction(MTL::LoadActionClear);
    att->setStoreAction(MTL::StoreActionStore);
    att->setClearColor(MTL::ClearColor::Make(color.r, color.g, color.b, color.a));

    MTL::RenderCommandEncoder* encoder = HW.pCurrentCommandBuffer->renderCommandEncoder(desc);
    desc->release();
    if (encoder)
        encoder->endEncoding();
}

void metalRenderPassManager::ClearImmediateDepth(
    MTL::Texture* texture, bool clearDepth, float depth, bool clearStencil, u8 stencil)
{
    if (!HW.pCurrentCommandBuffer)
        return;

    EndEncoder();

    MTL::RenderPassDescriptor* desc = MTL::RenderPassDescriptor::alloc()->init();

    MTL::RenderPassDepthAttachmentDescriptor* att = desc->depthAttachment();
    att->setTexture(texture);
    att->setLoadAction(clearDepth ? MTL::LoadActionClear : MTL::LoadActionLoad);
    att->setStoreAction(MTL::StoreActionStore);
    att->setClearDepth(depth);

    if (has_stencil(texture->pixelFormat()))
    {
        MTL::RenderPassStencilAttachmentDescriptor* satt = desc->stencilAttachment();
        satt->setTexture(texture);
        satt->setLoadAction(clearStencil ? MTL::LoadActionClear : MTL::LoadActionLoad);
        satt->setStoreAction(MTL::StoreActionStore);
        satt->setClearStencil(stencil);
    }

    MTL::RenderCommandEncoder* encoder = HW.pCurrentCommandBuffer->renderCommandEncoder(desc);
    desc->release();
    if (encoder)
        encoder->endEncoding();
}

// ===========================================================================
// metalRenderPass — free-function forwards (the contract itself)
// ===========================================================================
namespace metalRenderPass
{
void InvalidatePass() { RPManager.InvalidatePass(); }
void RequestClearColor(uint64_t rt, const Fcolor& color) { RPManager.RequestClearColor(rt, color); }
void RequestClearDepth(uint64_t zb, float depth) { RPManager.RequestClearDepth(zb, depth); }
void RequestClearStencil(uint64_t zb, u8 stencil) { RPManager.RequestClearStencil(zb, stencil); }
void SetViewport(const D3D_VIEWPORT& viewport) { RPManager.SetViewport(viewport); }
void SetScissor(const Irect* rect) { RPManager.SetScissor(rect); }

MTL::RenderCommandEncoder* EnsureEncoder(const uint64_t (&rt)[4], uint64_t zb)
{
    return RPManager.EnsureEncoder(rt, zb);
}

MTL::RenderCommandEncoder* ActiveEncoder() { return RPManager.ActiveEncoder(); }
} // namespace metalRenderPass

// ===========================================================================
// metalPipeline — PSO cache
// ===========================================================================
namespace metalPipeline
{
// Full-value key: no hash-collision risk, cheap memcmp compare.  Fields are
// laid out without padding (3x u64, then 8x u32).
struct PSOKey
{
    u64 vs;
    u64 ps;
    u64 stateHash;
    u32 declHash;
    u32 colorWrite;
    u32 fmt[6]; // color 0..3, depth, stencil

    bool operator<(const PSOKey& other) const { return memcmp(this, &other, sizeof(PSOKey)) < 0; }
};

// Failed creations are cached as nullptr to avoid a per-draw retry storm —
// the error is logged once at creation time.
static xr_map<PSOKey, MTL::RenderPipelineState*> s_cache;

static MTL::PixelFormat resolve_color_format(uint64_t handle, u32 slot)
{
    if (handle)
        return reinterpret_cast<MTL::Texture*>(handle)->pixelFormat();
    if (0 == slot)
    {
        // Backbuffer — prefer the held drawable, fall back to the layer format
        if (HW.pCurrentDrawable)
            return HW.pCurrentDrawable->texture()->pixelFormat();
        if (HW.pMetalLayer)
            return HW.pMetalLayer->pixelFormat();
    }
    return MTL::PixelFormatInvalid;
}

// D3DCOLORWRITEENABLE_* (RED=1, GREEN=2, BLUE=4, ALPHA=8) -> MTL::ColorWriteMask
static MTL::ColorWriteMask to_mtl_color_write_mask(u32 mask)
{
    MTL::ColorWriteMask result = MTL::ColorWriteMaskNone;
    if (mask & D3DCOLORWRITEENABLE_RED)
        result |= MTL::ColorWriteMaskRed;
    if (mask & D3DCOLORWRITEENABLE_GREEN)
        result |= MTL::ColorWriteMaskGreen;
    if (mask & D3DCOLORWRITEENABLE_BLUE)
        result |= MTL::ColorWriteMaskBlue;
    if (mask & D3DCOLORWRITEENABLE_ALPHA)
        result |= MTL::ColorWriteMaskAlpha;
    return result;
}

MTL::RenderPipelineState* GetOrCreatePSO(uint64_t vs, uint64_t ps, SDeclaration* decl, metalState* state,
    u32 colorWriteMask, const uint64_t (&rt)[4], uint64_t zb)
{
    // vs/ps are MetalShaderRegistry handles (index+1; populated by the shader
    // cache, Task 16), resolved to MTL::Function* below via the registry.
    // ps == 0 is legal for depth-only passes (shadow maps) — Metal allows a
    // null fragment function when no color attachment is bound.
    VERIFY2(vs, "metalPipeline::GetOrCreatePSO: no vertex shader bound");
    if (!vs)
        return nullptr;

    PSOKey key;
    ZeroMemory(&key, sizeof(key));
    key.vs = vs;
    key.ps = ps;
    key.stateHash = state ? state->GetStateHash() : 0;
    key.declHash = (decl && !decl->dcl_code.empty()) ? MetalVertexInput::HashVertexDescriptor(decl->dcl_code.data()) : 0;
    key.colorWrite = colorWriteMask;
    for (u32 i = 0; i < 4; ++i)
        key.fmt[i] = u32(resolve_color_format(rt[i], i));
    const MTL::PixelFormat depthFormat = zb ? reinterpret_cast<MTL::Texture*>(zb)->pixelFormat() : MTL::PixelFormatInvalid;
    key.fmt[4] = u32(depthFormat);
    key.fmt[5] = u32(has_stencil(depthFormat) ? depthFormat : MTL::PixelFormatInvalid);

    const auto it = s_cache.find(key);
    if (it != s_cache.end())
        return it->second;

    // --- cache miss: build the pipeline ------------------------------------
    VERIFY(HW.pDevice);

    // vs/ps are MetalShaderRegistry handles (index+1), NOT MTL::Function*
    // pointers — resolve them through the registry. (Casting the handle
    // straight to a pointer fed 0x3 to setVertexFunction: → SIGSEGV.)
    MTL::Function* vsFn = MetalShaderRegistry::Function(vs);
    MTL::Function* psFn = ps ? MetalShaderRegistry::Function(ps) : nullptr;
    VERIFY2(vsFn, "metalPipeline: vertex shader handle did not resolve to a function");

    MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
    desc->setVertexFunction(vsFn);
    if (psFn)
        desc->setFragmentFunction(psFn);
    if (decl && decl->dcl)
        desc->setVertexDescriptor(reinterpret_cast<MTL::VertexDescriptor*>(decl->dcl));

    const D3D_BLEND_STATE* blend = state ? &state->GetBlendState() : nullptr;
    const MTL::ColorWriteMask writeMask = to_mtl_color_write_mask(colorWriteMask);

    for (u32 i = 0; i < 4; ++i)
    {
        if (key.fmt[i] == u32(MTL::PixelFormatInvalid))
            continue;
        MTL::RenderPipelineColorAttachmentDescriptor* att = desc->colorAttachments()->object(i);
        att->setPixelFormat(MTL::PixelFormat(key.fmt[i]));
        att->setWriteMask(writeMask);
        if (blend && blend->BlendEnable)
        {
            // D3D9 has a single blend state for all render targets
            att->setBlendingEnabled(true);
            att->setSourceRGBBlendFactor(metalStateUtils::ToMTLBlendFactor(blend->SrcBlend));
            att->setDestinationRGBBlendFactor(metalStateUtils::ToMTLBlendFactor(blend->DestBlend));
            att->setRgbBlendOperation(metalStateUtils::ToMTLBlendOperation(blend->BlendOp));
            att->setSourceAlphaBlendFactor(metalStateUtils::ToMTLBlendFactor(blend->SrcBlendAlpha));
            att->setDestinationAlphaBlendFactor(metalStateUtils::ToMTLBlendFactor(blend->DestBlendAlpha));
            att->setAlphaBlendOperation(metalStateUtils::ToMTLBlendOperation(blend->BlendOpAlpha));
        }
    }

    if (key.fmt[4] != u32(MTL::PixelFormatInvalid))
        desc->setDepthAttachmentPixelFormat(MTL::PixelFormat(key.fmt[4]));
    if (key.fmt[5] != u32(MTL::PixelFormatInvalid))
        desc->setStencilAttachmentPixelFormat(MTL::PixelFormat(key.fmt[5]));

    NS::Error* error = nullptr;
    MTL::RenderPipelineState* pso = HW.pDevice->newRenderPipelineState(desc, &error);
    desc->release();

    if (!pso)
    {
        const MetalShaderRegistry::Entry* vsE = MetalShaderRegistry::Get(vs);
        const MetalShaderRegistry::Entry* psE = MetalShaderRegistry::Get(ps);
        Msg("! [Metal] PSO creation failed (vs='%s' ps='%s' decl=%x state=%llx): %s",
            vsE ? vsE->name.c_str() : "?", psE ? psE->name.c_str() : "?",
            key.declHash, key.stateHash,
            (error && error->localizedDescription()) ? error->localizedDescription()->utf8String()
                                                     : "unknown error");
    }

    s_cache.emplace(key, pso);
    return pso;
}

void OnDeviceDestroy()
{
    for (auto& [key, pso] : s_cache)
        if (pso)
            pso->release();
    s_cache.clear();
}
} // namespace metalPipeline

} // namespace xray::render::RENDER_NAMESPACE

#endif // USE_METAL
