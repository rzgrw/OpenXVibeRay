// metalr_screenshot.cpp — frame capture for the Metal backend.
// Mirrors glr_screenshot.cpp: reads the backbuffer back to the CPU and saves
// a JPEG.  Metal readback is a blit-encoder copy of the current drawable into
// a shared-storage buffer, synchronized by splitting the frame command buffer.
#include "stdafx.h"
#pragma hdrstop

#include "xrCore/Media/Image.hpp"
#include "xrEngine/xrImage_Resampler.h"

#include "Layers/xrRenderMetal/metalHW.h"
#include "Layers/xrRenderMetal/metalRenderPassManager.h"

namespace xray::render::RENDER_NAMESPACE
{
using namespace XRay::Media;

// XXX: Provide full implementation (gamesave thumbnails etc.) — GL parity
void CRender::Screenshot(ScreenshotMode mode /*= SM_NORMAL*/, pcstr name /*= nullptr*/)
{
    switch (mode)
    {
    case SM_NORMAL:
    {
        if (!HW.pCurrentCommandBuffer || !HW.pCurrentDrawable || !HW.pDevice)
        {
            Msg("~ [Metal] Screenshot: no frame in flight — request ignored");
            return;
        }

        MTL::Texture* source = HW.pCurrentDrawable->texture();
        if (source->framebufferOnly())
        {
            // CHW::CreateDevice configures the layer with framebufferOnly=false
            Msg("! [Metal] Screenshot: drawable is framebufferOnly, cannot blit");
            return;
        }
        VERIFY(source->pixelFormat() == MTL::PixelFormatBGRA8Unorm);

        const u32 width = u32(source->width());
        const u32 height = u32(source->height());
        const u32 bytesPerRow = width * 4;

        MTL::Buffer* readback =
            HW.pDevice->newBuffer(size_t(bytesPerRow) * height, MTL::ResourceStorageModeShared);
        if (!readback)
        {
            Msg("! [Metal] Screenshot: readback buffer allocation failed (%ux%u)", width, height);
            return;
        }

        // Encoders must not overlap on a command buffer — end the open pass.
        metalRenderPass::InvalidatePass();

        MTL::BlitCommandEncoder* blit = HW.pCurrentCommandBuffer->blitCommandEncoder();
        blit->copyFromTexture(source, 0, 0, MTL::Origin(0, 0, 0), MTL::Size(width, height, 1),
            readback, 0, bytesPerRow, size_t(bytesPerRow) * height);
        blit->endEncoding();

        // Split the frame: commit everything recorded so far and wait for the
        // GPU, then hand CHW a fresh command buffer for the rest of the frame
        // (CHW::EndScene commits it as usual).
        HW.pCurrentCommandBuffer->commit();
        HW.pCurrentCommandBuffer->waitUntilCompleted();
        HW.pCurrentCommandBuffer->release();
        HW.pCurrentCommandBuffer = HW.pCommandQueue->commandBuffer();

        // BGRA8 (top-left origin) -> tightly packed RGB8
        xr_vector<u8> pixels;
        pixels.resize(size_t(width) * height * 3);
        const u8* src = static_cast<const u8*>(readback->contents());
        for (u32 y = 0; y < height; ++y)
        {
            const u8* row = src + size_t(y) * bytesPerRow;
            u8* dst = pixels.data() + size_t(y) * width * 3;
            for (u32 x = 0; x < width; ++x)
            {
                dst[x * 3 + 0] = row[x * 4 + 2]; // R
                dst[x * 3 + 1] = row[x * 4 + 1]; // G
                dst[x * 3 + 2] = row[x * 4 + 0]; // B
            }
        }
        readback->release();

        pcstr extension = "jpg";

        string64 time;
        string_path buf;
        xr_sprintf(buf, sizeof(buf), "ss_%s_%s_(%s).%s", Core.UserName, timestamp(time),
            g_pGameLevel ? g_pGameLevel->name().c_str() : "mainmenu", extension);

        IWriter* fs = FS.w_open("$screenshots$", buf);
        R_ASSERT(fs);

        // Metal readback is already top-down — no vertical flip (unlike GL)
        Image img{width, height, pixels.data(), ImageDataFormat::RGB8};
        if (!img.SaveJPEG(*fs, 100, false))
            Log("! Failed to make a screenshot.");

        FS.w_close(fs);
        break;
    }

    case SM_FOR_GAMESAVE:
        // XXX: Implement (GL backend skips this as well)
        break;

    default:
        VERIFY(!"CRender::Screenshot. This screenshot type is not supported for Metal.");
    }
}
} // namespace xray::render::RENDER_NAMESPACE
