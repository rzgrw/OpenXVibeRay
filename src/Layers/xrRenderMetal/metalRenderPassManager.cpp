// metalRenderPassManager.cpp — MTL::RenderPassDescriptor / RenderCommandEncoder
// lifecycle for the Metal backend. See metalRenderPassManager.h for the model.
#include "stdafx.h"

#include "metalRenderPassManager.h"
#include "metalHW.h"

#if defined(USE_METAL)

namespace xray::render::RENDER_NAMESPACE
{

metalRenderPassManager RPManager;

//-----------------------------------------------------------------------------
void metalRenderPassManager::OnDeviceCreate()
{
    m_dirty = true;
}

void metalRenderPassManager::OnDeviceDestroy()
{
    EndEncoder();
    m_visibilityBuffer = nullptr;
    for (u32 i = 0; i < MaxColorTargets; ++i)
    {
        m_rt[i] = 0;
        m_clearColor[i] = false;
    }
    m_zb = 0;
    m_clearDepth = m_clearStencil = false;
    m_dirty = true;
}

//-----------------------------------------------------------------------------
// State recording
//-----------------------------------------------------------------------------
void metalRenderPassManager::SetColorTarget(u32 index, uint64_t rt)
{
    VERIFY(index < MaxColorTargets);
    if (m_rt[index] == rt)
        return;
    m_rt[index] = rt;
    m_dirty = true;
}

void metalRenderPassManager::SetDepthTarget(uint64_t zb)
{
    if (m_zb == zb)
        return;
    m_zb = zb;
    m_dirty = true;
}

void metalRenderPassManager::RequestClearColor(uint64_t rt, const Fcolor& color)
{
    // Clear of a currently bound target: becomes MTL::LoadActionClear on the
    // next encoder. A clear always splits the pass.
    for (u32 i = 0; i < MaxColorTargets; ++i)
    {
        if (m_rt[i] == rt && (rt != 0 || i == 0))
        {
            m_clearColor[i] = true;
            m_clearColorValue[i] = color;
            m_dirty = true;
            return;
        }
    }

    // Clear of an unbound target: run a dedicated clear-only pass now.
    MTL::Texture* texture = rt ? reinterpret_cast<MTL::Texture*>(rt)
                               : (HW.pCurrentDrawable ? HW.pCurrentDrawable->texture() : nullptr);
    if (texture)
        ClearImmediateColor(texture, color);
}

void metalRenderPassManager::RequestClearDepth(uint64_t zb, float depth)
{
    if (zb == m_zb && m_zb != 0)
    {
        m_clearDepth = true;
        m_clearDepthValue = depth;
        m_dirty = true;
        return;
    }
    if (MTL::Texture* texture = reinterpret_cast<MTL::Texture*>(zb))
        ClearImmediateDepth(texture, true, depth, false, 0);
}

void metalRenderPassManager::RequestClearStencil(uint64_t zb, u8 stencil)
{
    if (zb == m_zb && m_zb != 0)
    {
        m_clearStencil = true;
        m_clearStencilValue = stencil;
        m_dirty = true;
        return;
    }
    if (MTL::Texture* texture = reinterpret_cast<MTL::Texture*>(zb))
        ClearImmediateDepth(texture, false, 1.f, true, stencil);
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

static bool has_stencil(MTL::PixelFormat fmt)
{
    // Apple Silicon has no 24-bit depth; Depth32Float_Stencil8 is the packed format we use.
    return fmt == MTL::PixelFormatDepth32Float_Stencil8 || fmt == MTL::PixelFormatStencil8 ||
        fmt == MTL::PixelFormatX32_Stencil8;
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
    m_dirty = true;
}

//-----------------------------------------------------------------------------
// Encoder lifecycle
//-----------------------------------------------------------------------------
MTL::RenderCommandEncoder* metalRenderPassManager::EnsureEncoder()
{
    if (m_encoder && !m_dirty)
        return m_encoder;

    EndEncoder();

    if (!HW.pCurrentCommandBuffer)
        return nullptr; // outside BeginScene/EndScene

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
        if (m_clearColor[i])
        {
            att->setLoadAction(MTL::LoadActionClear);
            const Fcolor& c = m_clearColorValue[i];
            att->setClearColor(MTL::ClearColor::Make(c.r, c.g, c.b, c.a));
            m_clearColor[i] = false; // one-shot
        }
        else
        {
            att->setLoadAction(MTL::LoadActionLoad);
        }
        haveAttachment = true;
        width = (u32)texture->width();
        height = (u32)texture->height();
    }

    if (MTL::Texture* depth = DepthTexture())
    {
        MTL::RenderPassDepthAttachmentDescriptor* att = desc->depthAttachment();
        att->setTexture(depth);
        att->setStoreAction(MTL::StoreActionStore);
        if (m_clearDepth)
        {
            att->setLoadAction(MTL::LoadActionClear);
            att->setClearDepth(m_clearDepthValue);
            m_clearDepth = false; // one-shot
        }
        else
        {
            att->setLoadAction(MTL::LoadActionLoad);
        }

        if (has_stencil(depth->pixelFormat()))
        {
            MTL::RenderPassStencilAttachmentDescriptor* satt = desc->stencilAttachment();
            satt->setTexture(depth);
            satt->setStoreAction(MTL::StoreActionStore);
            if (m_clearStencil)
            {
                satt->setLoadAction(MTL::LoadActionClear);
                satt->setClearStencil(m_clearStencilValue);
            }
            else
            {
                satt->setLoadAction(MTL::LoadActionLoad);
            }
        }
        m_clearStencil = false; // one-shot

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

    // Depth-only passes (shadow maps) still need explicit dimensions? No —
    // Metal derives them from the attachments. But set the default viewport
    // explicitly below so shared code that skips SetViewport still works.
    if (m_visibilityBuffer)
        desc->setVisibilityResultBuffer(m_visibilityBuffer);

    m_encoder = HW.pCurrentCommandBuffer->renderCommandEncoder(desc);
    desc->release();

    if (m_encoder)
    {
        m_encoder->retain();
        const MTL::Viewport viewport = { 0.0, 0.0, (double)width, (double)height, 0.0, 1.0 };
        m_encoder->setViewport(viewport);
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
    m_dirty = true;
}

//-----------------------------------------------------------------------------
// Immediate clears (target not currently bound)
//-----------------------------------------------------------------------------
void metalRenderPassManager::ClearImmediateColor(MTL::Texture* texture, const Fcolor& color)
{
    if (!HW.pCurrentCommandBuffer)
        return;

    // The dedicated clear pass invalidates any open encoder ordering; end it
    // first so command buffer encoders never overlap.
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

} // namespace xray::render::RENDER_NAMESPACE

#endif // USE_METAL
