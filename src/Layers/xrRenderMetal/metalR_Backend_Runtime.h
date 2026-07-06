#pragma once

// metalR_Backend_Runtime.h — inline implementations of CBackend methods for the
// Metal backend.  Mirrors glR_Backend_Runtime.h.
//
// Architecture notes (Task 11):
//  - CBackend members pFB / pRT[] / pZB and ps / vs / gs / pp are uint64_t
//    opaque handles (see R_Backend.h, USE_METAL branches).  Render-target
//    handles are MTL::Texture* encoded via reinterpret_cast; shader handles
//    are MTL::Function* encoded the same way (populated by the shader cache,
//    plan Task 16).  0 means "none".
//  - vb / ib are VertexBufferHandle / IndexBufferHandle, which under USE_METAL
//    are real MTL::Buffer* (see CommonTypes.h).
//  - Metal has no imperative glBindFramebuffer/glClear model: attachments and
//    load actions are fixed at render-pass creation.  CBackend therefore only
//    *records* attachment/clear/viewport changes; the render pass is
//    (re)configured lazily when a draw call actually needs an encoder.

#include "metalHW.h"
#include "metalState.h"
#include "metalStateUtils.h"

namespace xray::render::RENDER_NAMESPACE
{
// ===========================================================================
// Render-pass / encoder management contract
// ---------------------------------------------------------------------------
// Implemented by metalRenderPassManager (plan Task 19).  Expected behaviour:
//
//  * InvalidatePass() — the attachment set changed.  If a render command
//    encoder is open it must be ended (endEncoding()); the next
//    EnsureEncoder() begins a fresh pass from the handles passed to it.
//
//  * RequestClearColor/Depth/Stencil(handle, ...) — record a pending clear for
//    the given attachment (uint64_t-encoded MTL::Texture*).  When the next
//    render pass that binds this attachment begins, the pending clear becomes
//    MTL::LoadActionClear with the recorded value, then is discarded
//    (attachments without a pending clear use MTL::LoadActionLoad).
//    If a pass with the attachment bound is currently OPEN, the manager must
//    restart the pass so the clear takes effect immediately — GL/DX clears are
//    imperative and shared render code relies on that.  A pending clear that
//    is never consumed by a pass should be flushed with a dedicated small pass
//    before the texture is sampled or the frame ends (manager's job).
//
//  * EnsureEncoder(rt, zb) — return the active MTL::RenderCommandEncoder,
//    beginning a render pass on HW.pCurrentCommandBuffer if none is open.
//    Color attachments 0..3 come from rt[], depth/stencil from zb; pending
//    clears become load actions.  Sticky viewport/scissor state (below) must
//    be re-applied to every newly begun encoder.  Returns nullptr when no
//    command buffer / drawable is available (draw is dropped for this frame).
//
//  * ActiveEncoder() — currently open encoder or nullptr; never begins a pass.
//
//  * SetViewport / SetScissor — sticky state: applied to the active encoder
//    immediately (if any) and re-applied whenever a new encoder begins.  The
//    manager must clamp rects to the current attachment extents (Metal
//    validation errors on out-of-bounds scissor/viewport).  A null scissor
//    rect means "scissor disabled" — use a full-target rect.
//    Note: Metal render targets are top-left origin like D3D, so no Y-flip is
//    required (unlike the GL backend).
//
//  * Frame lifecycle: whoever begins the frame (Task 19) must also call
//    g_ConstantRing.OnFrameBegin() (metalConstantBuffer.h) exactly once per
//    frame — it recycles the transient constant-buffer slices used by
//    R_constants::flush().
// ===========================================================================
namespace metalRenderPass
{
void InvalidatePass();
void RequestClearColor(uint64_t rt, const Fcolor& color);
void RequestClearDepth(uint64_t zb, float depth);
void RequestClearStencil(uint64_t zb, u8 stencil);
MTL::RenderCommandEncoder* EnsureEncoder(const uint64_t (&rt)[4], uint64_t zb);
MTL::RenderCommandEncoder* ActiveEncoder();
void SetViewport(const D3D_VIEWPORT& viewport);
void SetScissor(const Irect* rect); // nullptr = disable (full-target scissor)
} // namespace metalRenderPass

// ===========================================================================
// Pipeline-state resolution contract
// ---------------------------------------------------------------------------
// Implemented alongside the shader cache (plan Tasks 16/19).  Combines
// metalState::GetStateHash() with the shader handles, the vertex declaration
// and the bound attachment formats into a cache key; on a miss it builds a
// MTL::RenderPipelineDescriptor (functions decoded from the uint64_t handles,
// vertex layout from decl->dcl_code, blend state from state->GetBlendState())
// and creates the pipeline on HW.pDevice.
// colorWriteMask is passed explicitly because CBackend::set_ColorWriteEnable
// can override the value stored in the state block.
// ===========================================================================
namespace metalPipeline
{
MTL::RenderPipelineState* GetOrCreatePSO(uint64_t vs, uint64_t ps, SDeclaration* decl, metalState* state,
    u32 colorWriteMask, const uint64_t (&rt)[4], uint64_t zb);
} // namespace metalPipeline

// Vertex stream binding slot for encoder->setVertexBuffer().
// NOTE: must be kept in sync with the MSL generated by the shader pipeline
// (Task 16 — SPIRV-Cross stage-in buffer index).  Vertex streams occupy
// indices METAL_VERTEX_STREAM_INDEX + N; the per-stage uniform block binds
// at METAL_CBUFFER_BIND_INDEX == 30 (Task 12, metalConstantBuffer.h).
constexpr u32 METAL_VERTEX_STREAM_INDEX = 0;

// ---------------------------------------------------------------------------
// Transforms
// ---------------------------------------------------------------------------

IC void CBackend::set_xform(u32 ID, const Fmatrix& M)
{
    stat.xforms++;
    // Fixed-function transforms are not used by the programmable pipeline;
    // same no-op as the GL backend.
}

// ---------------------------------------------------------------------------
// Framebuffer / render targets
// Metal has no framebuffer objects; pFB is kept only for interface parity
// with the GL backend and is never dereferenced.
// ---------------------------------------------------------------------------

IC uint64_t CBackend::get_FB()
{
    return pFB;
}

IC void CBackend::set_FB(uint64_t FB)
{
    if (FB != pFB)
    {
        PGO(Msg("PGO:set_FB"));
        pFB = FB;
    }
}

IC void CBackend::set_RT(uint64_t RT, u32 ID)
{
    if (RT != pRT[ID])
    {
        PGO(Msg("PGO:setRT"));
        stat.target_rt++;
        pRT[ID] = RT;
        // Attachments changed — current render pass (if any) is stale.
        metalRenderPass::InvalidatePass();
    }
}

IC void CBackend::set_ZB(uint64_t ZB)
{
    if (ZB != pZB)
    {
        PGO(Msg("PGO:setZB"));
        stat.target_zb++;
        pZB = ZB;
        metalRenderPass::InvalidatePass();
    }
}

// ---------------------------------------------------------------------------
// Clears — recorded as pending load-action clears (see contract above).
// ---------------------------------------------------------------------------

IC void CBackend::ClearRT(uint64_t rt, const Fcolor& color)
{
    if (!rt)
        return;
    metalRenderPass::RequestClearColor(rt, color);
}

IC void CBackend::ClearZB(uint64_t zb, float depth)
{
    VERIFY(pZB == zb); // do not allow to clear unbound depth (GL parity)
    if (!zb)
        return;
    metalRenderPass::RequestClearDepth(zb, depth);
}

IC void CBackend::ClearZB(uint64_t zb, float depth, u8 stencil)
{
    VERIFY(pZB == zb); // do not allow to clear unbound depth (GL parity)
    if (!zb)
        return;
    metalRenderPass::RequestClearDepth(zb, depth);
    metalRenderPass::RequestClearStencil(zb, stencil);
}

IC bool CBackend::ClearRTRect(uint64_t /*rt*/, const Fcolor& /*color*/, size_t /*numRects*/, const Irect* /*rects*/)
{
    // Metal has no scissored clear; this needs a small quad-draw pass.
    // No engine call sites exist today — TODO(Task 19) if one appears.
    return true;
}

IC bool CBackend::ClearZBRect(uint64_t /*zb*/, float /*depth*/, size_t /*numRects*/, const Irect* /*rects*/)
{
    // See ClearRTRect. TODO(Task 19).
    return true;
}

// ---------------------------------------------------------------------------
// Vertex declaration — recorded only; consumed by PSO resolution at draw time.
// ---------------------------------------------------------------------------

ICF void CBackend::set_Format(SDeclaration* _decl)
{
    if (decl != _decl)
    {
        PGO(Msg("PGO:v_format:%p", _decl));
#ifdef DEBUG
        stat.decl++;
#endif
        decl = _decl;
        // Unlike GL (VAO change unbinds the element buffer), Metal keeps the
        // cached index buffer valid — no need to reset ib here.
    }
}

// ---------------------------------------------------------------------------
// Shader binding — records the MTL::Function* handles; the actual pipeline is
// resolved at draw time via metalPipeline::GetOrCreatePSO.
// ---------------------------------------------------------------------------

ICF void CBackend::set_PS(uint64_t _ps, LPCSTR _n)
{
    if (ps != _ps)
    {
        PGO(Msg("PGO:Pshader:%llu,%s", _ps, _n ? _n : ""));
        stat.ps++;
        ps = _ps;
#ifdef DEBUG
        ps_name = _n;
#endif
    }
}

ICF void CBackend::set_GS(uint64_t _gs, LPCSTR _n)
{
    if (gs != _gs)
    {
        stat.gs++;
        gs = _gs;
        // Metal has no geometry shaders — dead handle, kept for interface
        // parity. The shader cache must never produce a non-null gs on Metal;
        // techniques requiring GS need compute/vertex-shader rewrites.
#ifdef DEBUG
        gs_name = _n;
#endif
    }
}

ICF void CBackend::set_VS(uint64_t _vs, LPCSTR _n)
{
    if (vs != _vs)
    {
        PGO(Msg("PGO:Vshader:%llu,%s", _vs, _n ? _n : ""));
        stat.vs++;
        vs = _vs;
#ifdef DEBUG
        vs_name = _n;
#endif
    }
}

ICF void CBackend::set_PP(uint64_t _pp, pcstr _n)
{
    if (pp != _pp)
    {
        stat.pp++;
        pp = _pp;
        // GL-only concept (separate-shader-object program pipelines).  The
        // shared set_Pass() prefers pp over ps/vs when non-null, so the Metal
        // shader cache (Task 16) must keep SPass::pp null — otherwise ps/vs
        // stay stale and PSO resolution breaks.
        VERIFY2(_pp == 0, "set_PP: program pipelines are not supported on Metal");
#ifdef DEBUG
        pp_name = _n;
#endif
    }
}

// ---------------------------------------------------------------------------
// Vertex / index buffers — recorded; bound to the encoder at draw time.
// ---------------------------------------------------------------------------

ICF void CBackend::set_Vertices(VertexBufferHandle _vb, u32 _vb_stride)
{
    if (vb != _vb || vb_stride != _vb_stride)
    {
        PGO(Msg("PGO:VB:%p,%d", _vb, _vb_stride));
#ifdef DEBUG
        stat.vb++;
#endif
        vb = _vb;
        vb_stride = _vb_stride;
    }
}

ICF void CBackend::set_Indices(IndexBufferHandle _ib)
{
    if (ib != _ib)
    {
        PGO(Msg("PGO:IB:%p", _ib));
#ifdef DEBUG
        stat.ib++;
#endif
        ib = _ib;
    }
}

// ---------------------------------------------------------------------------
// Primitive topology translation
// ---------------------------------------------------------------------------

IC MTL::PrimitiveType TranslateTopology(D3DPRIMITIVETYPE T)
{
    switch (T)
    {
    case D3DPT_POINTLIST:     return MTL::PrimitiveTypePoint;
    case D3DPT_LINELIST:      return MTL::PrimitiveTypeLine;
    case D3DPT_LINESTRIP:     return MTL::PrimitiveTypeLineStrip;
    case D3DPT_TRIANGLELIST:  return MTL::PrimitiveTypeTriangle;
    case D3DPT_TRIANGLESTRIP: return MTL::PrimitiveTypeTriangleStrip;
    // D3DPT_TRIANGLEFAN has no Metal equivalent — callers must re-index.
    default: NODEFAULT;
    }
#ifdef DEBUG
    return MTL::PrimitiveTypeTriangle;
#endif
}

IC u32 GetIndexCount(D3DPRIMITIVETYPE T, u32 iPrimitiveCount)
{
    switch (T)
    {
    case D3DPT_POINTLIST:
        return iPrimitiveCount;
    case D3DPT_LINELIST:
        return iPrimitiveCount * 2;
    case D3DPT_LINESTRIP:
        return iPrimitiveCount + 1;
    case D3DPT_TRIANGLELIST:
        return iPrimitiveCount * 3;
    case D3DPT_TRIANGLESTRIP:
        return iPrimitiveCount + 2;
    default: NODEFAULT;
#ifdef DEBUG
        return 0;
#endif
    }
}

// ---------------------------------------------------------------------------
// Draw calls
// ---------------------------------------------------------------------------

ICF void CBackend::Render(D3DPRIMITIVETYPE T, u32 baseV, u32 startV, u32 countV, u32 startI, u32 PC)
{
    const MTL::PrimitiveType topology = TranslateTopology(T);
    const u32 iIndexCount = GetIndexCount(T, PC);

    stat.render.calls++;
    stat.render.verts += countV;
    stat.render.polys += PC;

    MTL::RenderCommandEncoder* encoder = metalRenderPass::EnsureEncoder(pRT, pZB);
    if (!encoder)
        return; // no drawable / command buffer this frame — drop the draw

    // Resolve the PSO for the current shader/layout/state/target combination
    MTL::RenderPipelineState* pso = metalPipeline::GetOrCreatePSO(vs, ps, decl, state, colorwrite_mask, pRT, pZB);
    VERIFY(pso);
    if (!pso)
        return;
    encoder->setRenderPipelineState(pso);

    // Depth/stencil state comes from the currently applied metalState.
    // TODO(Task 17/19): fold backend-level overrides (set_Z / set_ZFunc /
    // set_Stencil) into the DSS instead of relying on the SState values alone.
    if (state)
        encoder->setDepthStencilState(state->GetDepthStencilState());
    encoder->setStencilReferenceValue(stencil_ref);

    // Rasterizer state is imperative in Metal — apply per draw (cheap).
    // ToMTLCullMode maps D3DCULL_CW->Back / D3DCULL_CCW->Front, which assumes
    // counter-clockwise front faces; make that explicit on the encoder.
    encoder->setFrontFacingWinding(MTL::WindingCounterClockwise);
    encoder->setCullMode(metalStateUtils::ToMTLCullMode(cull_mode));
    encoder->setTriangleFillMode(metalStateUtils::ToMTLFillMode(fill_mode));

    // Bind stream 0; baseV goes into drawIndexedPrimitives as baseVertex.
    VERIFY(vb);
    encoder->setVertexBuffer(vb, 0, METAL_VERTEX_STREAM_INDEX);

    // Upload dirty shader constants (Task 12: setVertexBytes/setFragmentBytes)
    // Bind the current texture set now that the encoder exists. set_Textures
    // recorded these into textures_ps/textures_vs BEFORE EnsureEncoder created
    // the encoder, so the actual bind is deferred to draw time — the same
    // reason constants.flush() is deferred. (textures_ps[i] -> fragment slot i,
    // textures_vs[i] -> vertex slot i.)
    for (u32 ti = 0; ti < CTexture::mtMaxPixelShaderTextures; ++ti)
        if (textures_ps[ti])
            encoder->setFragmentTexture(reinterpret_cast<MTL::Texture*>(textures_ps[ti]->surface_get()), ti);
    for (u32 ti = 0; ti < CTexture::mtMaxVertexShaderTextures; ++ti)
        if (textures_vs[ti])
            encoder->setVertexTexture(reinterpret_cast<MTL::Texture*>(textures_vs[ti]->surface_get()), ti);

    constants.flush();

    // X-Ray uses 16-bit indices everywhere (GL: GL_UNSIGNED_SHORT).
    VERIFY(ib);
    encoder->drawIndexedPrimitives(topology, iIndexCount, MTL::IndexTypeUInt16, ib,
        startI * sizeof(u16), 1 /*instanceCount*/, (NS::Integer)baseV, 0 /*baseInstance*/);
    PGO(Msg("PGO:DIP:%dv/%df", countV, PC));
}

ICF void CBackend::Render(D3DPRIMITIVETYPE T, u32 startV, u32 PC)
{
    const MTL::PrimitiveType topology = TranslateTopology(T);
    const u32 iIndexCount = GetIndexCount(T, PC);

    stat.render.calls++;
    stat.render.verts += iIndexCount;
    stat.render.polys += PC;

    MTL::RenderCommandEncoder* encoder = metalRenderPass::EnsureEncoder(pRT, pZB);
    if (!encoder)
        return; // no drawable / command buffer this frame — drop the draw

    MTL::RenderPipelineState* pso = metalPipeline::GetOrCreatePSO(vs, ps, decl, state, colorwrite_mask, pRT, pZB);
    VERIFY(pso);
    if (!pso)
        return;
    encoder->setRenderPipelineState(pso);

    if (state)
        encoder->setDepthStencilState(state->GetDepthStencilState());
    encoder->setStencilReferenceValue(stencil_ref);

    encoder->setFrontFacingWinding(MTL::WindingCounterClockwise);
    encoder->setCullMode(metalStateUtils::ToMTLCullMode(cull_mode));
    encoder->setTriangleFillMode(metalStateUtils::ToMTLFillMode(fill_mode));

    VERIFY(vb);
    encoder->setVertexBuffer(vb, 0, METAL_VERTEX_STREAM_INDEX);

    // Bind the current texture set now that the encoder exists. set_Textures
    // recorded these into textures_ps/textures_vs BEFORE EnsureEncoder created
    // the encoder, so the actual bind is deferred to draw time — the same
    // reason constants.flush() is deferred. (textures_ps[i] -> fragment slot i,
    // textures_vs[i] -> vertex slot i.)
    for (u32 ti = 0; ti < CTexture::mtMaxPixelShaderTextures; ++ti)
        if (textures_ps[ti])
            encoder->setFragmentTexture(reinterpret_cast<MTL::Texture*>(textures_ps[ti]->surface_get()), ti);
    for (u32 ti = 0; ti < CTexture::mtMaxVertexShaderTextures; ++ti)
        if (textures_vs[ti])
            encoder->setVertexTexture(reinterpret_cast<MTL::Texture*>(textures_vs[ti]->surface_get()), ti);

    constants.flush();

    encoder->drawPrimitives(topology, (NS::UInteger)startV, (NS::UInteger)iIndexCount);
    PGO(Msg("PGO:DP:%dv/%df", iIndexCount, PC));
}

// ---------------------------------------------------------------------------
// Geometry convenience
// ---------------------------------------------------------------------------

IC void CBackend::set_Geometry(SGeometry* _geom)
{
    set_Format(&*_geom->dcl);
    set_Vertices(_geom->vb, _geom->vb_stride);
    set_Indices(_geom->ib);
}

// ---------------------------------------------------------------------------
// Scissor / viewport — sticky state, owned by the render-pass manager so it
// can be re-applied whenever a new encoder begins.
// ---------------------------------------------------------------------------

IC void CBackend::set_Scissor(const Irect* R)
{
    metalRenderPass::SetScissor(R);
}

IC void CBackend::SetViewport(const D3D_VIEWPORT& viewport) const
{
    metalRenderPass::SetViewport(viewport);
}

// ---------------------------------------------------------------------------
// Depth / stencil / rasterizer overrides
// These record backend-level state.  stencil_ref, cull_mode and fill_mode are
// consumed at draw time above; the remaining depth/stencil overrides must be
// folded into DSS creation later (TODO Task 17/19 — currently the DSS comes
// from the SState-owned metalState only, matching most render paths where
// set_Stencil follows set_Element on the same state).
// ---------------------------------------------------------------------------

IC void CBackend::set_Stencil(u32 _enable, u32 _func, u32 _ref, u32 _mask, u32 _writemask, u32 _fail, u32 _pass,
                              u32 _zfail)
{
    stencil_enable = _enable;
    stencil_func = _func;
    stencil_ref = _ref;
    stencil_mask = _mask;
    stencil_writemask = _writemask;
    stencil_fail = _fail;
    stencil_pass = _pass;
    stencil_zfail = _zfail;
    // stencil_ref is applied per draw via setStencilReferenceValue().
}

IC void CBackend::set_Z(u32 _enable)
{
    if (z_enable != _enable)
    {
        z_enable = _enable;
        // TODO(Task 17/19): rebuild/override MTL::DepthStencilState
    }
}

IC void CBackend::set_ZFunc(u32 _func)
{
    if (z_func != _func)
    {
        z_func = _func;
        // TODO(Task 17/19): rebuild/override MTL::DepthStencilState
    }
}

IC void CBackend::set_AlphaRef(u32 _value)
{
    if (alpha_ref != _value)
    {
        alpha_ref = _value;
        // Metal has no fixed-function alpha test; the shader pipeline
        // (Task 16) emulates it in the fragment shader using this constant.
    }
}

IC void CBackend::set_ColorWriteEnable(u32 _mask)
{
    if (colorwrite_mask != _mask)
    {
        colorwrite_mask = _mask;
        // Baked into the PSO — consumed by GetOrCreatePSO at draw time.
    }
}

ICF void CBackend::set_CullMode(u32 _mode)
{
    if (cull_mode != _mode)
    {
        cull_mode = _mode;
        // Applied to the encoder at draw time.
    }
}

ICF void CBackend::set_FillMode(u32 _mode)
{
    if (fill_mode != _mode)
    {
        fill_mode = _mode;
        // Applied to the encoder at draw time.
    }
}

ICF void CBackend::SetTextureFactor(u32 /*factor*/) const
{
    // Not supported (fixed-function leftover)
}

ICF void CBackend::SetAmbient(u32 /*factor*/) const
{
    // Not supported (fixed-function leftover)
}

ICF void CBackend::set_VS(ref_vs& _vs)
{
    set_VS(_vs->sh, _vs->cName.c_str());
}

// ---------------------------------------------------------------------------
// Constant table binding — identical to the GL backend.
// ---------------------------------------------------------------------------

IC void CBackend::set_Constants(R_constant_table* C)
{
    // caching
    if (ctable == C)
        return;
    ctable = C;
    constants.set_table(C); // Metal: pull the per-stage uniform block sizes
    xforms.unmap();
    hemi.unmap();
    tree.unmap();
    if (nullptr == C)
        return;

    PGO(Msg("PGO:c-table"));

    // process constant-loaders
    for (auto& Cs : C->table)
        if (Cs->handler)
            Cs->handler->setup(*this, &*Cs);
}

// ---------------------------------------------------------------------------
// Render-target pass setup — records attachments; the actual
// MTL::RenderPassDescriptor is built lazily by EnsureEncoder at draw time.
// ---------------------------------------------------------------------------

void CBackend::set_pass_targets(const ref_rt& _1, const ref_rt& _2, const ref_rt& _3, const ref_rt& zb)
{
    if (_1)
    {
        curr_rt_width = _1->dwWidth;
        curr_rt_height = _1->dwHeight;
    }
    else
    {
        VERIFY(zb);
        curr_rt_width = zb->dwWidth;
        curr_rt_height = zb->dwHeight;
    }

    set_RT(_1 ? _1->pRT : 0, 0);
    set_RT(_2 ? _2->pRT : 0, 1);
    set_RT(_3 ? _3->pRT : 0, 2);
    set_ZB(zb ? zb->pZRT : 0);

    const D3D_VIEWPORT viewport = {0, 0, curr_rt_width, curr_rt_height, 0.f, 1.f};
    SetViewport(viewport);
}
} // namespace xray::render::RENDER_NAMESPACE
