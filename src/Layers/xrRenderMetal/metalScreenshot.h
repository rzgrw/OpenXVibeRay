#pragma once
// Deferred screenshot capture for the Metal backend.
//
// A screenshot request can arrive at any point in the frame (e.g. the agent
// bridge drains it at ProcessFrame start, when no drawable exists yet). The
// CAMetalDrawable is only valid between BeginScene and Present, so we LATCH the
// request and service it in CHW::EndScene — after phase_flip has copied the
// finished frame into the drawable and while the command buffer is still open.
#include "xrCore/xrCore.h"

namespace MTL { class CommandBuffer; class Drawable; class Device; }

namespace xray::render::RENDER_NAMESPACE
{
// Latch a request (called from CRender::Screenshot). name may be null.
void MetalScreenshot_Request(pcstr name);

// True if a request is pending — lets EndScene decide to wait on the GPU.
bool MetalScreenshot_Pending();

// Record the drawable->readback blit onto the still-open frame command buffer
// (call after phase_flip, before commit). No-op if nothing is pending.
void MetalScreenshot_RecordBlit(MTL::CommandBuffer* cb, MTL::Drawable* drawable, MTL::Device* dev);

// After the command buffer has completed, read the buffer back and write the
// JPEG, then clear the pending state. No-op if nothing was recorded.
void MetalScreenshot_Finalize();
} // namespace xray::render::RENDER_NAMESPACE
