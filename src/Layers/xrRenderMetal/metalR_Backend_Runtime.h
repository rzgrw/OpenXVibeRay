#pragma once

// metalR_Backend_Runtime.h — stub inline implementations of CBackend methods
// for the Metal backend. Mirrors glR_Backend_Runtime.h.
// All bodies are empty/no-ops; real Metal encoding will be implemented later.

namespace xray::render::RENDER_NAMESPACE
{

IC uint64_t CBackend::get_FB()
{
    return pFB;
}

IC void CBackend::set_FB(uint64_t FB)
{
    pFB = FB;
    // TODO: Switch active Metal render pass framebuffer
}

IC void CBackend::set_RT(uint64_t RT, u32 ID)
{
    if (RT != pRT[ID])
    {
        stat.target_rt++;
        pRT[ID] = RT;
        // TODO: Attach RT to current Metal render pass descriptor
    }
}

IC void CBackend::set_ZB(uint64_t ZB)
{
    if (ZB != pZB)
    {
        stat.target_zb++;
        pZB = ZB;
        // TODO: Attach depth/stencil texture to current Metal render pass descriptor
    }
}

IC void CBackend::ClearRT(uint64_t /*rt*/, const Fcolor& /*color*/)
{
    // TODO: Use MTLLoadActionClear on the color attachment
}

IC void CBackend::ClearZB(uint64_t /*zb*/, float /*depth*/)
{
    // TODO: Use MTLLoadActionClear on the depth attachment
}

IC void CBackend::ClearZB(uint64_t /*zb*/, float /*depth*/, u8 /*stencil*/)
{
    // TODO: Use MTLLoadActionClear on depth+stencil attachments
}

IC bool CBackend::ClearRTRect(uint64_t /*rt*/, const Fcolor& /*color*/, size_t /*numRects*/, const Irect* /*rects*/)
{
    // TODO: Implement scissored clear via a short render pass
    return true;
}

IC bool CBackend::ClearZBRect(uint64_t /*zb*/, float /*depth*/, size_t /*numRects*/, const Irect* /*rects*/)
{
    // TODO: Implement scissored depth clear via a short render pass
    return true;
}

ICF void CBackend::set_Format(SDeclaration* _decl)
{
    if (decl != _decl)
    {
#ifdef DEBUG
        stat.decl++;
#endif
        decl = _decl;
        // TODO: Bind Metal vertex descriptor / pipeline state
    }
}

ICF void CBackend::set_PS(uint64_t _ps, LPCSTR /*_n*/)
{
    if (ps != _ps)
    {
        stat.ps++;
        ps = _ps;
        // TODO: Record fragment function into pipeline state
    }
}

ICF void CBackend::set_VS(uint64_t _vs, LPCSTR /*_n*/)
{
    if (vs != _vs)
    {
        stat.vs++;
        vs = _vs;
        // TODO: Record vertex function into pipeline state
    }
}

ICF void CBackend::set_GS(uint64_t _gs, LPCSTR /*_n*/)
{
    if (gs != _gs)
    {
        stat.gs++;
        gs = _gs;
        // Metal does not support geometry shaders; stub kept for interface parity
    }
}

ICF void CBackend::set_PP(uint64_t _pp, pcstr /*_n*/)
{
    if (pp != _pp)
    {
        stat.pp++;
        pp = _pp;
        // TODO: Set pipeline program object
    }
}

ICF void CBackend::set_Vertices(uint64_t _vb, u32 _vb_stride)
{
    if (vb != _vb || vb_stride != _vb_stride)
    {
#ifdef DEBUG
        stat.vb++;
#endif
        vb         = _vb;
        vb_stride  = _vb_stride;
        // TODO: setVertexBuffer:offset:atIndex: on render encoder
    }
}

ICF void CBackend::set_Indices(uint64_t _ib)
{
    if (ib != _ib)
    {
#ifdef DEBUG
        stat.ib++;
#endif
        ib = _ib;
        // TODO: Store index buffer for use in drawIndexedPrimitives
    }
}

ICF void CBackend::Render(D3DPRIMITIVETYPE /*T*/, u32 /*baseV*/, u32 /*startV*/, u32 /*countV*/, u32 /*startI*/, u32 PC)
{
    stat.render.calls++;
    stat.render.polys += PC;
    constants.flush();
    // TODO: Encode drawIndexedPrimitives onto the Metal render encoder
}

ICF void CBackend::Render(D3DPRIMITIVETYPE /*T*/, u32 /*startV*/, u32 PC)
{
    stat.render.calls++;
    stat.render.polys += PC;
    constants.flush();
    // TODO: Encode drawPrimitives onto the Metal render encoder
}

IC void CBackend::set_Geometry(SGeometry* _geom)
{
    set_Format(&*_geom->dcl);
    set_Vertices(_geom->vb, _geom->vb_stride);
    set_Indices(_geom->ib);
}

IC void CBackend::set_xform(u32 /*ID*/, const Fmatrix& /*M*/)
{
    stat.xforms++;
    // TODO: Push transform constant to Metal shader
}

IC void CBackend::set_Scissor(const Irect* R)
{
    if (R)
    {
        // TODO: setScissorRect on the render encoder
    }
    else
    {
        // TODO: Disable/reset scissor rect
    }
}

IC void CBackend::SetViewport(const D3D_VIEWPORT& /*viewport*/) const
{
    // TODO: setViewport on the render encoder
}

IC void CBackend::set_Stencil(u32 /*_enable*/, u32 /*_func*/, u32 /*_ref*/, u32 /*_mask*/,
                               u32 /*_writemask*/, u32 /*_fail*/, u32 /*_pass*/, u32 /*_zfail*/)
{
    // TODO: Map to MTLDepthStencilDescriptor / setStencilReferenceValue
}

IC void CBackend::set_Z(u32 /*_enable*/)
{
    // TODO: Toggle depth testing via MTLDepthStencilDescriptor
}

IC void CBackend::set_ZFunc(u32 /*_func*/)
{
    // TODO: Set depthCompareFunction in MTLDepthStencilDescriptor
}

IC void CBackend::set_AlphaRef(u32 /*_value*/)
{
    // Not supported in Metal (alpha testing is done in shader)
}

IC void CBackend::set_ColorWriteEnable(u32 _mask)
{
    colorwrite_mask = _mask;
    // TODO: Set writeMask on MTLRenderPipelineColorAttachmentDescriptor
}

ICF void CBackend::set_CullMode(u32 _mode)
{
    cull_mode = _mode;
    // TODO: setCullMode on render encoder
}

ICF void CBackend::set_FillMode(u32 _mode)
{
    fill_mode = _mode;
    // TODO: setTriangleFillMode on render encoder
}

ICF void CBackend::SetTextureFactor(u32 /*factor*/) const
{
    // Not supported in Metal
}

ICF void CBackend::SetAmbient(u32 /*factor*/) const
{
    // Not supported
}

ICF void CBackend::set_VS(ref_vs& _vs)
{
    set_VS(_vs->sh, _vs->cName.c_str());
}

IC void CBackend::set_Constants(R_constant_table* C)
{
    if (ctable == C) return;
    ctable = C;
    xforms.unmap();
    hemi.unmap();
    tree.unmap();
    if (nullptr == C) return;

    for (auto& Cs : C->table)
        if (Cs->handler) Cs->handler->setup(*this, &*Cs);
}

void CBackend::set_pass_targets(const ref_rt& _1, const ref_rt& _2, const ref_rt& _3, const ref_rt& zb)
{
    if (_1)
    {
        curr_rt_width  = _1->dwWidth;
        curr_rt_height = _1->dwHeight;
    }
    else
    {
        VERIFY(zb);
        curr_rt_width  = zb->dwWidth;
        curr_rt_height = zb->dwHeight;
    }

    set_RT(_1 ? _1->pRT : 0, 0);
    set_RT(_2 ? _2->pRT : 0, 1);
    set_RT(_3 ? _3->pRT : 0, 2);
    set_ZB(zb ? zb->pZRT : 0);

    const D3D_VIEWPORT viewport = { 0, 0, curr_rt_width, curr_rt_height, 0.f, 1.f };
    SetViewport(viewport);
}

} // namespace xray::render::RENDER_NAMESPACE
