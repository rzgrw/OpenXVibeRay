#include "stdafx.h"
#pragma hdrstop

#include "Layers/xrRenderMetal/metalConstantBuffer.h"
#include "Layers/xrRenderMetal/metalHW.h"

namespace xray::render::RENDER_NAMESPACE
{
MetalConstantRing g_ConstantRing;

//-----------------------------------------------------------------------------
// MetalConstantBuffer
//-----------------------------------------------------------------------------

void MetalConstantBuffer::Create(u32 size)
{
    VERIFY2(0 == m_size && nullptr == m_buffer, "MetalConstantBuffer::Create called twice");
    VERIFY(size);
    VERIFY(HW.pDevice);

    m_size = size;
    m_cpuData = xr_alloc<u8>(size);
    ZeroMemory(m_cpuData, size);

    // Apple Silicon is UMA — shared storage gives coherent zero-copy access.
    m_buffer = HW.pDevice->newBuffer(size, MTL::ResourceStorageModeShared);
    R_ASSERT2(m_buffer, "Metal: failed to create constant buffer");

    m_dirty = true; // force the initial (zeroed) upload
}

void MetalConstantBuffer::Destroy()
{
    if (m_buffer)
    {
        m_buffer->release();
        m_buffer = nullptr;
    }
    if (m_cpuData)
        xr_free(m_cpuData);
    m_size = 0;
    m_dirty = false;
}

void MetalConstantBuffer::Write(u32 offset, const void* data, u32 size)
{
    VERIFY(m_cpuData);
    VERIFY2(offset + size <= m_size, "MetalConstantBuffer::Write out of bounds");

    CopyMemory(m_cpuData + offset, data, size);
    m_dirty = true;
}

void MetalConstantBuffer::Bind(MTL::RenderCommandEncoder* encoder, u32 index, bool vertex)
{
    VERIFY(encoder);
    if (!m_buffer)
        return;

    if (m_dirty)
    {
        // NOTE: in-place update — safe only for at-most-once-per-frame data
        // (see header). Per-draw constants go through MetalConstantRing.
        CopyMemory(m_buffer->contents(), m_cpuData, m_size);
        m_dirty = false;
    }

    if (vertex)
        encoder->setVertexBuffer(m_buffer, 0, index);
    else
        encoder->setFragmentBuffer(m_buffer, 0, index);
}

//-----------------------------------------------------------------------------
// MetalConstantRing
//-----------------------------------------------------------------------------

void MetalConstantRing::Destroy()
{
    for (Page& page : m_pages)
    {
        if (page.buffer)
            page.buffer->release();
    }
    m_pages.clear();
    m_slot = 0;
    m_generation = 0;
}

void MetalConstantRing::OnFrameBegin()
{
    ++m_generation;
    m_slot = m_generation % kFramesInFlight;

    // Recycle the pages of the slot we are entering — the GPU finished with
    // them kFramesInFlight frames ago.
    for (Page& page : m_pages)
    {
        if (page.slot == m_slot)
            page.used = 0;
    }
}

MetalConstantRing::Slice MetalConstantRing::Alloc(u32 size)
{
    VERIFY(size);

    const u32 alignedSize = (size + kSliceAlignment - 1) & ~(kSliceAlignment - 1);

    // First fit within the current frame slot
    for (Page& page : m_pages)
    {
        if (page.slot != m_slot)
            continue;
        if (page.used + alignedSize > (u32)page.buffer->length())
            continue;

        Slice slice;
        slice.buffer = page.buffer;
        slice.offset = page.used;
        slice.cpu = static_cast<u8*>(page.buffer->contents()) + page.used;
        page.used += alignedSize;
        return slice;
    }

    // Grow: add a page to the current slot
    VERIFY(HW.pDevice);
    const u32 pageSize = std::max(alignedSize, (u32)kPageSize);
    MTL::Buffer* buffer = HW.pDevice->newBuffer(pageSize, MTL::ResourceStorageModeShared);
    if (!buffer)
    {
        Msg("! Metal: constant ring page allocation failed (%u bytes)", pageSize);
        return {};
    }

    Page& page = m_pages.emplace_back();
    page.buffer = buffer;
    page.used = alignedSize;
    page.slot = m_slot;

    Slice slice;
    slice.buffer = buffer;
    slice.offset = 0;
    slice.cpu = static_cast<u8*>(buffer->contents());
    return slice;
}
} // namespace xray::render::RENDER_NAMESPACE
