#include "stdafx.h"
#pragma hdrstop

#include "Layers/xrRender/BufferUtils.h"
#include "Layers/xrRenderMetal/metalHW.h"
#include "Layers/xrRenderMetal/metalVertexInput.h"

#include <FlexibleVertexFormat.h>

namespace xray::render::RENDER_NAMESPACE
{
u32 GetFVFVertexSize(u32 FVF)
{
    return static_cast<u32>(::FVF::ComputeVertexSize(FVF));
}

u32 GetDeclVertexSize(const VertexElement* decl, u32 Stream)
{
    return static_cast<u32>(::FVF::ComputeVertexSize(decl, Stream));
}

u32 GetDeclLength(const VertexElement* decl)
{
    return static_cast<u32>(::FVF::GetDeclLength(decl));
}

// Apple Silicon is UMA — MTL::ResourceStorageModeShared gives coherent
// zero-copy CPU/GPU access for every buffer type; write-combined CPU cache
// mode for buffers the CPU only ever writes.
static HRESULT CreateBuffer(MTL::Buffer** ppBuffer, const void* pData, u32 dataSize, bool bDynamic)
{
    VERIFY(ppBuffer);
    VERIFY(dataSize);
    VERIFY(HW.pDevice);

    const MTL::ResourceOptions options = MTL::ResourceStorageModeShared |
        (bDynamic ? MTL::ResourceCPUCacheModeWriteCombined : MTL::ResourceCPUCacheModeDefaultCache);

    MTL::Buffer* buffer = pData
        ? HW.pDevice->newBuffer(pData, dataSize, options)
        : HW.pDevice->newBuffer(dataSize, options);

    if (!buffer)
    {
        Msg("! Metal: failed to create %s buffer, size=%u", bDynamic ? "dynamic" : "static", dataSize);
        *ppBuffer = nullptr;
        return E_FAIL;
    }

    *ppBuffer = buffer;
    return S_OK;
}

static inline HRESULT CreateVertexBuffer(VertexBufferHandle* ppBuffer, const void* pData, u32 dataSize, bool bDynamic)
{
    return CreateBuffer(ppBuffer, pData, dataSize, bDynamic);
}

static inline HRESULT CreateIndexBuffer(IndexBufferHandle* ppBuffer, const void* pData, u32 dataSize, bool bDynamic)
{
    return CreateBuffer(ppBuffer, pData, dataSize, bDynamic);
}

static inline void DestroyBuffer(MTL::Buffer*& buffer)
{
    if (buffer)
    {
        buffer->release();
        buffer = nullptr;
    }
}

namespace BufferUtils
{
HRESULT CreateConstantBuffer(ConstantBufferHandle* ppBuffer, u32 DataSize)
{
    return CreateBuffer(ppBuffer, nullptr, DataSize, true);
}
};

// Builds the MTL::VertexDescriptor for the declaration and stores it in
// SDeclaration::dcl (uint64_t handle).  Consumed by the PSO cache
// (metalPipeline::GetOrCreatePSO, plan Tasks 16/19).  The descriptor is
// owned by the SDeclaration — its teardown must release() it (Task 14).
void ConvertVertexDeclaration(const VertexElement* dxdecl, SDeclaration* decl)
{
    VERIFY(decl);
    MTL::VertexDescriptor* descriptor = MetalVertexInput::CreateVertexDescriptor(dxdecl);
    decl->dcl = reinterpret_cast<uint64_t>(descriptor);
}

//-----------------------------------------------------------------------------
VertexStagingBuffer::~VertexStagingBuffer()
{
    Destroy();
}

void VertexStagingBuffer::Create(size_t size, bool allowReadBack /*= false*/)
{
    m_Size = size;
    m_AllowReadBack = allowReadBack;

    m_HostBuffer = xr_alloc<u8>(size);
    AddRef();
}

bool VertexStagingBuffer::IsValid() const
{
    return !!m_DeviceBuffer;
}

void* VertexStagingBuffer::Map(
    size_t offset /*= 0*/,
    size_t size /*= 0*/,
    bool read /*= false*/)
{
    VERIFY2(m_HostBuffer, "Buffer wasn't created or already discarded");
    VERIFY2(!read || m_AllowReadBack, "Can't read from write only buffer");
    VERIFY2((size + offset) <= m_Size, "Map region is too large");

    return static_cast<u8*>(m_HostBuffer) + offset;
}

void VertexStagingBuffer::Unmap(bool doFlush /*= false*/)
{
    if (!doFlush)
    {
        /* Do nothing*/
        return;
    }

    VERIFY2(!m_DeviceBuffer, "Attempting to upload buffer twice");
    VERIFY(m_HostBuffer && m_Size);

    // Upload data to the device
    CreateVertexBuffer(&m_DeviceBuffer, m_HostBuffer, m_Size, false);

    if (!m_AllowReadBack)
    {
        // Cache buffer isn't required anymore. Free host memory
        DiscardHostBuffer();
    }
}

VertexBufferHandle VertexStagingBuffer::GetBufferHandle() const
{
    return m_DeviceBuffer;
}

void VertexStagingBuffer::Destroy()
{
    DiscardHostBuffer();
    m_Size = 0;

    DestroyBuffer(m_DeviceBuffer);
}

void VertexStagingBuffer::DiscardHostBuffer()
{
    if (m_HostBuffer)
        xr_free(m_HostBuffer);
}

size_t VertexStagingBuffer::GetSystemMemoryUsage() const
{
    return m_HostBuffer ? m_Size : 0;
}

size_t VertexStagingBuffer::GetVideoMemoryUsage() const
{
    return m_DeviceBuffer ? static_cast<size_t>(m_DeviceBuffer->length()) : 0;
}

//-----------------------------------------------------------------------------
IndexStagingBuffer::~IndexStagingBuffer()
{
    Destroy();
}

void IndexStagingBuffer::Create(size_t size, bool allowReadBack /*= false*/, bool /*managed = true*/)
{
    m_Size = size;
    m_AllowReadBack = allowReadBack;

    m_HostBuffer = xr_alloc<u8>(size);
    AddRef();
}

bool IndexStagingBuffer::IsValid() const
{
    return !!m_DeviceBuffer;
}

void* IndexStagingBuffer::Map(
    size_t offset /*= 0*/,
    size_t size /*= 0*/,
    bool read /*= false*/)
{
    VERIFY2(m_HostBuffer, "Buffer wasn't created or already discarded");
    VERIFY2(!read || m_AllowReadBack, "Can't read from write only buffer");
    VERIFY2((size + offset) <= m_Size, "Map region is too large");

    return static_cast<u8*>(m_HostBuffer) + offset;
}

void IndexStagingBuffer::Unmap(bool doFlush /*= false*/)
{
    if (!doFlush)
    {
        /* Do nothing*/
        return;
    }

    VERIFY2(!m_DeviceBuffer, "Attempting to upload buffer twice");
    VERIFY(m_HostBuffer && m_Size);

    // Upload data to the device
    CreateIndexBuffer(&m_DeviceBuffer, m_HostBuffer, m_Size, false);

    if (!m_AllowReadBack)
    {
        // Cache buffer isn't required anymore. Free host memory
        DiscardHostBuffer();
    }
}

IndexBufferHandle IndexStagingBuffer::GetBufferHandle() const
{
    return m_DeviceBuffer;
}

void IndexStagingBuffer::Destroy()
{
    DiscardHostBuffer();
    m_Size = 0;

    DestroyBuffer(m_DeviceBuffer);
}

void IndexStagingBuffer::DiscardHostBuffer()
{
    if (m_HostBuffer)
        xr_free(m_HostBuffer);
}

size_t IndexStagingBuffer::GetSystemMemoryUsage() const
{
    return m_HostBuffer ? m_Size : 0;
}

size_t IndexStagingBuffer::GetVideoMemoryUsage() const
{
    return m_DeviceBuffer ? static_cast<size_t>(m_DeviceBuffer->length()) : 0;
}

//-----------------------------------------------------------------------------
// Stream buffers — mutable ring-style buffers written through shared storage.
// Shared-mode MTL::Buffer contents are always CPU-visible and coherent, so
// Map simply returns a pointer and Unmap is a no-op.
//
// NOTE: like the GL backend (GL_MAP_UNSYNCHRONIZED_BIT), synchronization
// against in-flight GPU reads is the CALLER's responsibility — the engine's
// dynamic streams (R_DStreams) already cycle their own append offsets.
//-----------------------------------------------------------------------------
VertexStreamBuffer::~VertexStreamBuffer()
{
    Destroy();
}

void VertexStreamBuffer::Create(size_t size)
{
    CreateVertexBuffer(&m_DeviceBuffer, nullptr, size, true);
    AddRef();
}

void VertexStreamBuffer::Destroy()
{
    DestroyBuffer(m_DeviceBuffer);
}

void* VertexStreamBuffer::Map(size_t offset, size_t size, bool flush /*= false*/)
{
    VERIFY(m_DeviceBuffer);
    VERIFY((offset + size) <= m_DeviceBuffer->length());

    return static_cast<u8*>(m_DeviceBuffer->contents()) + offset;
}

void VertexStreamBuffer::Unmap()
{
    VERIFY(m_DeviceBuffer);
    // Shared storage is coherent — nothing to flush.
}

bool VertexStreamBuffer::IsValid() const
{
    return !!m_DeviceBuffer;
}

//-----------------------------------------------------------------------------
IndexStreamBuffer::~IndexStreamBuffer()
{
    Destroy();
}

void IndexStreamBuffer::Create(size_t size)
{
    CreateIndexBuffer(&m_DeviceBuffer, nullptr, size, true);
    AddRef();
}

void IndexStreamBuffer::Destroy()
{
    DestroyBuffer(m_DeviceBuffer);
}

void* IndexStreamBuffer::Map(size_t offset, size_t size, bool flush /*= false*/)
{
    VERIFY(m_DeviceBuffer);
    VERIFY((offset + size) <= m_DeviceBuffer->length());

    return static_cast<u8*>(m_DeviceBuffer->contents()) + offset;
}

void IndexStreamBuffer::Unmap()
{
    VERIFY(m_DeviceBuffer);
    // Shared storage is coherent — nothing to flush.
}

bool IndexStreamBuffer::IsValid() const
{
    return !!m_DeviceBuffer;
}
} // namespace xray::render::RENDER_NAMESPACE
