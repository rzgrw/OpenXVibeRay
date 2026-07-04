#pragma once

// metalRenderPassManager — owns the MTL::RenderPassDescriptor lifecycle for the
// Metal backend.
//
// Metal has no mutable framebuffer object: render targets, load/store actions
// and clear values are baked into a MTL::RenderPassDescriptor at the moment a
// MTL::RenderCommandEncoder is created, and cannot change afterwards.  The
// X-Ray shared render code, however, is written against the D3D/GL model of
// "bind RT, maybe clear, draw".  This class bridges the two models:
//
//   * CBackend::set_RT/set_ZB (metalR_Backend_Runtime.h) only RECORD the
//     uint64_t attachment handles here and mark the pass dirty.
//   * CBackend::ClearRT/ClearZB record pending clear requests; they become
//     MTL::LoadActionClear on the next encoder, or - when the target is not
//     currently bound - an immediate dedicated clear pass.
//   * The first draw (CBackend::Render) calls EnsureEncoder(), which lazily
//     ends the previous encoder and begins a new one when anything changed.
//
// Handle convention (see SH_RT.h / SH_Texture.h USE_METAL branches):
//   uint64_t attachment handles are reinterpret_cast MTL::Texture* pointers;
//   handle 0 on color slot 0 means "the backbuffer" (current CAMetalDrawable),
//   handle 0 elsewhere means "no attachment".

#if defined(USE_METAL)
#include <Metal/Metal.hpp>
#endif

namespace xray::render::RENDER_NAMESPACE
{

class metalRenderPassManager
{
public:
    static constexpr u32 MaxColorTargets = 4;

    void OnDeviceCreate();
    void OnDeviceDestroy();

    // --- state recording (called from the CBackend inline runtime) ----------
    void SetColorTarget(u32 index, uint64_t rt); // rt == 0: backbuffer on slot 0, none otherwise
    void SetDepthTarget(uint64_t zb);            // zb == 0: no depth attachment

    void RequestClearColor(uint64_t rt, const Fcolor& color);
    void RequestClearDepth(uint64_t zb, float depth);
    void RequestClearStencil(uint64_t zb, u8 stencil);

#if defined(USE_METAL)
    // --- encoder lifecycle ---------------------------------------------------
    // Returns the encoder for the currently recorded attachment state,
    // beginning a new render pass when needed. May return nullptr when no
    // command buffer / attachments are available (e.g. between frames).
    MTL::RenderCommandEncoder* EnsureEncoder();
    MTL::RenderCommandEncoder* CurrentEncoder() const { return m_encoder; }
    void EndEncoder();

    // Must be called before the frame command buffer is committed/presented.
    void OnFrameEnd() { EndEncoder(); }

    // Attachment pixel formats of the CURRENT recorded state — used for
    // MTL::RenderPipelineDescriptor creation (PSO formats must match the pass).
    MTL::PixelFormat ColorFormat(u32 index = 0) const;
    MTL::PixelFormat DepthFormat() const;
    MTL::PixelFormat StencilFormat() const;

    // Visibility-result buffer for occlusion queries. Attached to every
    // render pass descriptor created afterwards (Metal requires it to be set
    // before the encoder is created).
    void SetVisibilityBuffer(MTL::Buffer* buffer);

private:
    MTL::Texture* ColorTexture(u32 index) const;
    MTL::Texture* DepthTexture() const;
    // Standalone pass that only performs a load-action clear (used when a
    // clear is requested for a target that is not currently bound).
    void ClearImmediateColor(MTL::Texture* texture, const Fcolor& color);
    void ClearImmediateDepth(MTL::Texture* texture, bool clearDepth, float depth, bool clearStencil, u8 stencil);
#endif // USE_METAL

private:
    // Recorded attachment handles (uint64_t — see header comment)
    uint64_t m_rt[MaxColorTargets] = {};
    uint64_t m_zb = 0;

    // Pending clear requests, consumed by the next EnsureEncoder()
    bool   m_clearColor[MaxColorTargets] = {};
    Fcolor m_clearColorValue[MaxColorTargets] = {};
    bool   m_clearDepth = false;
    float  m_clearDepthValue = 1.f;
    bool   m_clearStencil = false;
    u8     m_clearStencilValue = 0;

    // True when the recorded state no longer matches the open encoder
    bool m_dirty = true;

#if defined(USE_METAL)
    MTL::RenderCommandEncoder* m_encoder = nullptr;
    MTL::Buffer* m_visibilityBuffer = nullptr;
#endif
};

extern metalRenderPassManager RPManager;

} // namespace xray::render::RENDER_NAMESPACE
