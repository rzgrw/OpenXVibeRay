// metal_rendertarget_u_set_rt.cpp — u_setrt overload family for the Metal
// backend.  Mirrors gl_rendertarget_u_set_rt.cpp.
//
// Metal has no framebuffer objects: set_RT/set_ZB only record the uint64_t
// attachment handles on CBackend and invalidate the current render pass; the
// MTL::RenderPassDescriptor is built lazily by metalRenderPass::EnsureEncoder
// when the next draw call needs an encoder (see metalR_Backend_Runtime.h).
// There is therefore no set_FB / glDrawBuffers / completeness-check
// equivalent here.

#include "stdafx.h"

namespace xray::render::RENDER_NAMESPACE
{
void CRenderTarget::u_setrt(CBackend& cmd_list, const ref_rt& _1, const ref_rt& _2, const ref_rt& _3, const ref_rt& _zb)
{
    dwWidth[cmd_list.context_id] = 0;
    dwHeight[cmd_list.context_id] = 0;

    if (_1)
    {
        dwWidth[cmd_list.context_id]  = _1->dwWidth;
        dwHeight[cmd_list.context_id] = _1->dwHeight;

        cmd_list.set_RT(_1->pRT, 0);
    }
    else
    {
        cmd_list.set_RT(0, 0);
    }

    if (_2)
    {
        if (dwWidth[cmd_list.context_id] && dwHeight[cmd_list.context_id])
        {
            VERIFY(_2->dwWidth  == dwWidth[cmd_list.context_id]);
            VERIFY(_2->dwHeight == dwHeight[cmd_list.context_id]);
        }
        else
        {
            dwWidth[cmd_list.context_id]  = _2->dwWidth;
            dwHeight[cmd_list.context_id] = _2->dwHeight;
        }

        cmd_list.set_RT(_2->pRT, 1);
    }
    else
    {
        cmd_list.set_RT(0, 1);
    }

    if (_3)
    {
        if (dwWidth[cmd_list.context_id] && dwHeight[cmd_list.context_id])
        {
            VERIFY(_3->dwWidth  == dwWidth[cmd_list.context_id]);
            VERIFY(_3->dwHeight == dwHeight[cmd_list.context_id]);
        }
        else
        {
            dwWidth[cmd_list.context_id]  = _3->dwWidth;
            dwHeight[cmd_list.context_id] = _3->dwHeight;
        }

        cmd_list.set_RT(_3->pRT, 2);
    }
    else
    {
        cmd_list.set_RT(0, 2);
    }

    if (_zb)
    {
        if (dwWidth[cmd_list.context_id] && dwHeight[cmd_list.context_id])
        {
            VERIFY(_zb->dwWidth  == dwWidth[cmd_list.context_id]);
            VERIFY(_zb->dwHeight == dwHeight[cmd_list.context_id]);
        }
        else
        {
            dwWidth[cmd_list.context_id]  = _zb->dwWidth;
            dwHeight[cmd_list.context_id] = _zb->dwHeight;
        }

        cmd_list.set_ZB(_zb->pZRT);
    }
    else
    {
        cmd_list.set_ZB(0);
    }

    VERIFY(dwWidth[cmd_list.context_id]  != 0);
    VERIFY(dwHeight[cmd_list.context_id] != 0);
}

void CRenderTarget::u_setrt(CBackend& cmd_list, const ref_rt& _1, const ref_rt& _2, const ref_rt& _zb)
{
    dwWidth[cmd_list.context_id]  = 0;
    dwHeight[cmd_list.context_id] = 0;

    if (_1)
    {
        dwWidth[cmd_list.context_id]  = _1->dwWidth;
        dwHeight[cmd_list.context_id] = _1->dwHeight;

        cmd_list.set_RT(_1->pRT, 0);
    }
    else
    {
        cmd_list.set_RT(0, 0);
    }

    if (_2)
    {
        if (dwWidth[cmd_list.context_id] && dwHeight[cmd_list.context_id])
        {
            VERIFY(_2->dwWidth  == dwWidth[cmd_list.context_id]);
            VERIFY(_2->dwHeight == dwHeight[cmd_list.context_id]);
        }
        else
        {
            dwWidth[cmd_list.context_id]  = _2->dwWidth;
            dwHeight[cmd_list.context_id] = _2->dwHeight;
        }

        cmd_list.set_RT(_2->pRT, 1);
    }
    else
    {
        cmd_list.set_RT(0, 1);
    }

    // GL leaves color attachment 2 bound but masks it via glDrawBuffers;
    // Metal has no draw-buffer mask, so unbind the stale attachment instead.
    cmd_list.set_RT(0, 2);

    if (_zb)
    {
        if (dwWidth[cmd_list.context_id] && dwHeight[cmd_list.context_id])
        {
            VERIFY(_zb->dwWidth  == dwWidth[cmd_list.context_id]);
            VERIFY(_zb->dwHeight == dwHeight[cmd_list.context_id]);
        }
        else
        {
            dwWidth[cmd_list.context_id]  = _zb->dwWidth;
            dwHeight[cmd_list.context_id] = _zb->dwHeight;
        }

        cmd_list.set_ZB(_zb->pZRT);
    }
    else
    {
        cmd_list.set_ZB(0);
    }

    VERIFY(dwWidth[cmd_list.context_id]  != 0);
    VERIFY(dwHeight[cmd_list.context_id] != 0);
}

void CRenderTarget::u_setrt(CBackend& cmd_list, u32 W, u32 H, uint64_t _1, uint64_t _2, uint64_t _3, uint64_t zb)
{
    VERIFY(W != 0);
    VERIFY(H != 0);

    dwWidth[cmd_list.context_id]  = W;
    dwHeight[cmd_list.context_id] = H;

    cmd_list.set_RT(_1, 0);
    cmd_list.set_RT(_2, 1);
    cmd_list.set_RT(_3, 2);
    cmd_list.set_ZB(zb);
}
} // namespace xray::render::RENDER_NAMESPACE
