// metalr_screenshot.cpp — frame capture for the Metal backend.
// Mirrors glr_screenshot.cpp, but Metal's CAMetalDrawable only exists mid-frame,
// so capture is DEFERRED: CRender::Screenshot latches the request and
// CHW::EndScene services it (drawable->readback blit on the open command buffer,
// then a GPU wait + readback + JPEG). See metalScreenshot.h.
#include "stdafx.h"
#pragma hdrstop

#include "xrCore/Media/Image.hpp"
#include "xrEngine/xrImage_Resampler.h"

#include "Layers/xrRenderMetal/metalHW.h"
#include "Layers/xrRenderMetal/metalScreenshot.h"

namespace xray::render::RENDER_NAMESPACE
{
using namespace XRay::Media;

namespace
{
    bool s_pending = false;          // a request is latched
    string_path s_name{};            // requested name ("" = timestamped)
    MTL::Buffer* s_readback = nullptr; // populated by RecordBlit, consumed by Finalize
    u32 s_w = 0, s_h = 0;
} // namespace

void MetalScreenshot_Request(pcstr name)
{
    s_pending = true;
    if (name && name[0])
        xr_strcpy(s_name, name);
    else
        s_name[0] = 0;
}

bool MetalScreenshot_Pending() { return s_pending; }

void MetalScreenshot_RecordBlit(MTL::CommandBuffer* cb, MTL::Drawable* drawable, MTL::Device* dev)
{
    if (!s_pending || !cb || !drawable || !dev)
        return;

    auto* md = static_cast<CA::MetalDrawable*>(drawable);
    MTL::Texture* source = md->texture();
    if (!source || source->framebufferOnly())
    {
        Msg("! [Metal] Screenshot: drawable unavailable/framebufferOnly, cannot blit");
        s_pending = false;
        return;
    }

    s_w = u32(source->width());
    s_h = u32(source->height());
    const u32 bytesPerRow = s_w * 4;

    s_readback = dev->newBuffer(size_t(bytesPerRow) * s_h, MTL::ResourceStorageModeShared);
    if (!s_readback)
    {
        Msg("! [Metal] Screenshot: readback alloc failed (%ux%u)", s_w, s_h);
        s_pending = false;
        return;
    }

    MTL::BlitCommandEncoder* blit = cb->blitCommandEncoder();
    blit->copyFromTexture(source, 0, 0, MTL::Origin(0, 0, 0), MTL::Size(s_w, s_h, 1),
        s_readback, 0, bytesPerRow, size_t(bytesPerRow) * s_h);
    blit->endEncoding();
}

void MetalScreenshot_Finalize()
{
    if (!s_pending)
        return;
    s_pending = false;
    if (!s_readback)
        return; // RecordBlit bailed; already logged

    const u32 bytesPerRow = s_w * 4;
    // BGRA8 (top-left origin) -> tightly packed RGB8 (already top-down; no flip)
    xr_vector<u8> pixels;
    pixels.resize(size_t(s_w) * s_h * 3);
    const u8* src = static_cast<const u8*>(s_readback->contents());
    for (u32 y = 0; y < s_h; ++y)
    {
        const u8* row = src + size_t(y) * bytesPerRow;
        u8* dst = pixels.data() + size_t(y) * s_w * 3;
        for (u32 x = 0; x < s_w; ++x)
        {
            dst[x * 3 + 0] = row[x * 4 + 2]; // R
            dst[x * 3 + 1] = row[x * 4 + 1]; // G
            dst[x * 3 + 2] = row[x * 4 + 0]; // B
        }
    }
    s_readback->release();
    s_readback = nullptr;

    string_path buf;
    if (s_name[0])
        xr_sprintf(buf, sizeof(buf), "%s.jpg", s_name);
    else
    {
        string64 time;
        xr_sprintf(buf, sizeof(buf), "ss_%s_%s_(%s).jpg", Core.UserName, timestamp(time),
            g_pGameLevel ? g_pGameLevel->name().c_str() : "mainmenu");
    }

    IWriter* fs = FS.w_open("$screenshots$", buf);
    if (!fs)
    {
        Msg("! [Metal] Screenshot: cannot open '%s'", buf);
        return;
    }
    Image img{ s_w, s_h, pixels.data(), ImageDataFormat::RGB8 };
    if (!img.SaveJPEG(*fs, 100, false))
        Log("! [Metal] Failed to encode screenshot.");
    else
        Msg("* [Metal] Screenshot saved: %s (%ux%u)", buf, s_w, s_h);
    FS.w_close(fs);
}

void CRender::Screenshot(ScreenshotMode mode /*= SM_NORMAL*/, pcstr name /*= nullptr*/)
{
    switch (mode)
    {
    case SM_NORMAL:
        // Latch — serviced by CHW::EndScene when the drawable is live.
        MetalScreenshot_Request(name);
        break;

    case SM_FOR_GAMESAVE:
        // XXX: Implement (GL backend skips this as well)
        break;

    default:
        VERIFY(!"CRender::Screenshot. This screenshot type is not supported for Metal.");
    }
}
} // namespace xray::render::RENDER_NAMESPACE
