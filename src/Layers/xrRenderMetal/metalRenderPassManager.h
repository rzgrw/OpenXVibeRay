#pragma once

// metalRenderPassManager — owns the MTL::RenderPassDescriptor / encoder
// lifecycle for the Metal backend and implements the metalRenderPass
// free-function contract documented in metalR_Backend_Runtime.h.
//
// Metal has no mutable framebuffer object: render targets, load/store actions
// and clear values are baked into a MTL::RenderPassDescriptor at the moment a
// MTL::RenderCommandEncoder is created, and cannot change afterwards.  The
// X-Ray shared render code, however, is written against the D3D/GL model of
// "bind RT, maybe clear, draw".  This manager bridges the two models:
//
//   * CBackend::set_RT/set_ZB (metalR_Backend_Runtime.h) only record the
//     uint64_t attachment handles on the CBackend and call InvalidatePass();
//     the handles travel to the manager with the next EnsureEncoder(rt, zb).
//   * CBackend::ClearRT/ClearZB record pending clears KEYED BY HANDLE.  They
//     become MTL::LoadActionClear on the next pass that binds the attachment.
//     If a pass with the attachment bound is currently open it is restarted so
//     the clear takes effect at the request point (GL/DX imperative-clear
//     parity).  Pending clears not consumed by any pass are flushed with
//     dedicated clear-only passes at OnFrameEnd().
//     TODO(Task 20): also flush from CTexture::apply when a texture with a
//     pending clear is about to be sampled mid-frame.
//   * Viewport and scissor are sticky: recorded here, applied to the active
//     encoder immediately and re-applied (clamped to the attachment extents)
//     to every newly begun encoder.  Metal render targets are top-left origin
//     like D3D — no Y-flip (unlike the GL backend).
//   * OnFrameBegin()/OnFrameEnd() are called from CHW::BeginScene/EndScene;
//     OnFrameBegin also recycles the transient constant-buffer ring
//     (g_ConstantRing, see metalConstantBuffer.h).
//
// Handle convention (see SH_RT.h / SH_Texture.h USE_METAL branches):
//   uint64_t attachment handles are reinterpret_cast MTL::Texture* pointers;
//   handle 0 on color slot 0 means "the backbuffer" (current CAMetalDrawable),
//   handle 0 elsewhere means "no attachment".  CBackend::ClearRT/ClearZB
//   filter out handle 0, so pending clears only ever target real textures;
//   the backbuffer is never cleared explicitly (the combine pass overwrites
//   every pixel).

#include "Layers/xrRenderMetal/CommonTypes.h"

namespace xray::render::RENDER_NAMESPACE
{
struct SDeclaration;

class metalRenderPassManager
{
public:
    static constexpr u32 MaxColorTargets = 4;

    void OnDeviceCreate();
    void OnDeviceDestroy();

    // Frame lifecycle — called from CHW::BeginScene / CHW::EndScene.
    void OnFrameBegin(); // recycles g_ConstantRing, invalidates cached pass
    void OnFrameEnd();   // ends the encoder, flushes unconsumed pending clears

    // --- metalRenderPass contract (see metalR_Backend_Runtime.h) ------------
    void InvalidatePass();
    void RequestClearColor(uint64_t rt, const Fcolor& color);
    void RequestClearDepth(uint64_t zb, float depth);
    void RequestClearStencil(uint64_t zb, u8 stencil);
    void SetViewport(const D3D_VIEWPORT& viewport);
    void SetScissor(const Irect* rect); // nullptr = disable (full-target rect)

#if defined(USE_METAL)
    // Returns the encoder for the given attachment handles, beginning a new
    // render pass when anything changed.  Returns nullptr when no command
    // buffer / attachments are available (draw is dropped for this frame).
    MTL::RenderCommandEncoder* EnsureEncoder(const uint64_t (&rt)[MaxColorTargets], uint64_t zb);
    // Re-begin on the most recently recorded attachments (consumers that draw
    // "into whatever is bound", e.g. the ImGui backend).
    MTL::RenderCommandEncoder* EnsureEncoder();
    MTL::RenderCommandEncoder* ActiveEncoder() const { return m_encoder; }
    void EndEncoder();

    // Attachment pixel formats of the current recorded state — used for
    // MTL::RenderPipelineDescriptor creation (PSO formats must match the pass).
    MTL::PixelFormat ColorFormat(u32 index = 0) const;
    MTL::PixelFormat DepthFormat() const;
    MTL::PixelFormat StencilFormat() const;

    // Visibility-result buffer for occlusion queries.  Attached to every
    // render pass descriptor created afterwards (Metal requires it to be set
    // before the encoder is created).
    void SetVisibilityBuffer(MTL::Buffer* buffer);

private:
    MTL::Texture* ColorTexture(u32 index) const;
    MTL::Texture* DepthTexture() const;
    // Re-apply the sticky viewport/scissor to the open encoder, clamped to
    // the current attachment extents.
    void ApplyViewportAndScissor();
    // Frame-end fallback for pending clears no pass consumed.
    void FlushPendingClears();
    // Standalone passes that only perform a load-action clear.
    void ClearImmediateColor(MTL::Texture* texture, const Fcolor& color);
    void ClearImmediateDepth(MTL::Texture* texture, bool clearDepth, float depth, bool clearStencil, u8 stencil);
#endif // USE_METAL

private:
    struct PendingColorClear
    {
        uint64_t rt;
        Fcolor color;
    };
    struct PendingDepthClear
    {
        uint64_t zb;
        bool depth;
        float depthValue;
        bool stencil;
        u8 stencilValue;
    };

    // Attachment handles most recently passed to EnsureEncoder (see header)
    uint64_t m_rt[MaxColorTargets] = {};
    uint64_t m_zb = 0;

    // Pending clears keyed by attachment handle (never handle 0)
    xr_vector<PendingColorClear> m_pendingColor;
    xr_vector<PendingDepthClear> m_pendingDepth;

    // Sticky viewport/scissor state
    XR_METAL_VIEWPORT m_viewport{};
    bool m_viewportValid = false;
    Irect m_scissor{};
    bool m_scissorEnabled = false;

    // Extents of the currently open pass (clamp target for viewport/scissor)
    u32 m_passWidth = 0;
    u32 m_passHeight = 0;

    // True when the recorded state no longer matches the open encoder
    bool m_dirty = true;

#if defined(USE_METAL)
    MTL::RenderCommandEncoder* m_encoder = nullptr;
    MTL::Buffer* m_visibilityBuffer = nullptr;
#endif
};

extern metalRenderPassManager RPManager;

// ===========================================================================
// metalRenderPass — the free-function contract consumed by the CBackend
// inline runtime (metalR_Backend_Runtime.h), QueryHelper.h and the ImGui
// backend.  Thin forwards to RPManager.
// ===========================================================================
namespace metalRenderPass
{
void InvalidatePass();
void RequestClearColor(uint64_t rt, const Fcolor& color);
void RequestClearDepth(uint64_t zb, float depth);
void RequestClearStencil(uint64_t zb, u8 stencil);
void SetViewport(const D3D_VIEWPORT& viewport);
void SetScissor(const Irect* rect); // nullptr = disable (full-target scissor)
#if defined(USE_METAL)
MTL::RenderCommandEncoder* EnsureEncoder(const uint64_t (&rt)[4], uint64_t zb);
MTL::RenderCommandEncoder* ActiveEncoder();
#endif
} // namespace metalRenderPass

// ===========================================================================
// metalPipeline — MTL::RenderPipelineState cache (contract documented in
// metalR_Backend_Runtime.h).  Keyed on shader handles, vertex-layout hash,
// render-state hash, color-write mask and the bound attachment formats.
// ===========================================================================
namespace metalPipeline
{
#if defined(USE_METAL)
MTL::RenderPipelineState* GetOrCreatePSO(uint64_t vs, uint64_t ps, SDeclaration* decl, metalState* state,
    u32 colorWriteMask, const uint64_t (&rt)[4], uint64_t zb);
#endif
// Releases every cached pipeline state (called from RPManager device teardown)
void OnDeviceDestroy();
} // namespace metalPipeline

} // namespace xray::render::RENDER_NAMESPACE
