// metal_rendertarget_phase_flip.cpp — present path for the Metal backend.
//
// GL never runs its (historical, #if 0) phase_flip: the final image lands in
// rt_Base through the pFB framebuffer, and CHW::Present blits pFB into the
// window with glBlitFramebuffer.  On Metal rt_Base is likewise a plain
// offscreen texture (metalSH_RT has no swapchain wiring), so the equivalent
// step is a blit-encoder copy of rt_Base into the frame's CAMetalDrawable.
// It must be encoded into the frame's command buffer *before* it is
// committed, therefore CHW::EndScene (metalHW.cpp) calls phase_flip() after
// RPManager.OnFrameEnd() (no render encoder may be open when the blit
// encoder is created) and before commit; CHW::Present then just presents the
// drawable.
//
// Both textures are BGRA8Unorm (HW.Caps.fTarget == D3DFMT_A8R8G8B8 maps to
// PixelFormatBGRA8Unorm — see metalTextureUtils.cpp — and the layer format is
// set in CHW::CreateDevice), which blit copies require.
//
// TODO Task 20: MTL::BlitCommandEncoder cannot scale.  When the drawable size
// differs from the render resolution (HiDPI, window clamped by the desktop),
// GL letterboxes with a scaling glBlitFramebuffer; here we copy the
// overlapping region centered instead.  A scaled/letterboxed present needs a
// fullscreen-quad draw (or MPSImageBilinearScale).

#include "stdafx.h"

namespace xray::render::RENDER_NAMESPACE
{
void CRenderTarget::phase_flip()
{
    if (!HW.pCurrentCommandBuffer || !HW.pCurrentDrawable)
        return; // no frame in flight — nothing to present

    MTL::Texture* source = reinterpret_cast<MTL::Texture*>(get_base_rt());
    MTL::Texture* drawable = HW.pCurrentDrawable->texture();
    if (!source || !drawable)
        return;

    if (source->pixelFormat() != drawable->pixelFormat())
    {
        // Blit copies require matching formats; align HW.Caps.fTarget with the
        // layer format (both BGRA8) if this ever fires.
        static bool warned = false;
        if (!warned)
        {
            warned = true;
            Msg("! [Metal] phase_flip: rt_Base format (%d) != drawable format (%d), present skipped",
                int(source->pixelFormat()), int(drawable->pixelFormat()));
        }
        return;
    }

    // No open render encoder is allowed while a blit encoder records —
    // CHW::EndScene calls us after RPManager.OnFrameEnd() ended it.
    VERIFY(!metalRenderPass::ActiveEncoder());

    const NS::UInteger w = std::min(source->width(), drawable->width());
    const NS::UInteger h = std::min(source->height(), drawable->height());
    const NS::UInteger dstX = (drawable->width() - w) / 2;
    const NS::UInteger dstY = (drawable->height() - h) / 2;

    MTL::BlitCommandEncoder* blit = HW.pCurrentCommandBuffer->blitCommandEncoder();
    if (!blit)
        return;
    blit->copyFromTexture(source, 0 /*slice*/, 0 /*level*/, MTL::Origin::Make(0, 0, 0), MTL::Size::Make(w, h, 1),
        drawable, 0 /*slice*/, 0 /*level*/, MTL::Origin::Make(dstX, dstY, 0));
    blit->endEncoding();
}
} // namespace xray::render::RENDER_NAMESPACE
