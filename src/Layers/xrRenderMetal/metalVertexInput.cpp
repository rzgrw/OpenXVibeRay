#include "stdafx.h"
#pragma hdrstop

#include "Layers/xrRender/BufferUtils.h"
#include "Layers/xrRenderMetal/metalVertexInput.h"

namespace xray::render::RENDER_NAMESPACE
{
// D3DDECLTYPE -> MTL::VertexFormat.
// Indexed by D3DDECLTYPE (0 .. D3DDECLTYPE_FLOAT16_4).
static const MTL::VertexFormat VertexFormatList[] =
{
    MTL::VertexFormatFloat, // D3DDECLTYPE_FLOAT1
    MTL::VertexFormatFloat2, // D3DDECLTYPE_FLOAT2
    MTL::VertexFormatFloat3, // D3DDECLTYPE_FLOAT3
    MTL::VertexFormatFloat4, // D3DDECLTYPE_FLOAT4
    // D3DCOLOR is stored as BGRA bytes; like the DX11 backend
    // (DXGI_FORMAT_R8G8B8A8_UNORM) we fetch it as RGBA-normalized and rely
    // on the shaders' explicit component swizzling.
    MTL::VertexFormatUChar4Normalized, // D3DDECLTYPE_D3DCOLOR
    MTL::VertexFormatUChar4, // D3DDECLTYPE_UBYTE4
    MTL::VertexFormatShort2, // D3DDECLTYPE_SHORT2
    MTL::VertexFormatShort4, // D3DDECLTYPE_SHORT4
    MTL::VertexFormatUChar4Normalized, // D3DDECLTYPE_UBYTE4N
    MTL::VertexFormatShort2Normalized, // D3DDECLTYPE_SHORT2N
    MTL::VertexFormatShort4Normalized, // D3DDECLTYPE_SHORT4N
    MTL::VertexFormatUShort2Normalized, // D3DDECLTYPE_USHORT2N
    MTL::VertexFormatUShort4Normalized, // D3DDECLTYPE_USHORT4N
    MTL::VertexFormatInvalid, // D3DDECLTYPE_UDEC3 (no Metal equivalent)
    MTL::VertexFormatInt1010102Normalized, // D3DDECLTYPE_DEC3N
    MTL::VertexFormatHalf2, // D3DDECLTYPE_FLOAT16_2
    MTL::VertexFormatHalf4 // D3DDECLTYPE_FLOAT16_4
};

// D3DDECLUSAGE -> attribute location. Mirrors GL's VertexUsageList so the
// same shader-side attribute numbering can be reused (see header note).
static const u32 VertexUsageList[] =
{
    3, // D3DDECLUSAGE_POSITION
    ~0u, // D3DDECLUSAGE_BLENDWEIGHT
    ~0u, // D3DDECLUSAGE_BLENDINDICES
    5, // D3DDECLUSAGE_NORMAL
    ~0u, // D3DDECLUSAGE_PSIZE
    8, // D3DDECLUSAGE_TEXCOORD
    4, // D3DDECLUSAGE_TANGENT
    6, // D3DDECLUSAGE_BINORMAL
    ~0u, // D3DDECLUSAGE_TESSFACTOR
    3, // D3DDECLUSAGE_POSITIONT
    0, // D3DDECLUSAGE_COLOR
    7, // D3DDECLUSAGE_FOG
    ~0u, // D3DDECLUSAGE_DEPTH
    ~0u, // D3DDECLUSAGE_SAMPLE
};

MTL::VertexFormat MetalVertexInput::TranslateType(u32 type)
{
    if (type >= std::size(VertexFormatList))
    {
        VERIFY(!"MetalVertexInput::TranslateType: unknown D3DDECLTYPE");
        return MTL::VertexFormatInvalid;
    }
    return VertexFormatList[type];
}

u32 MetalVertexInput::AttributeLocation(u32 usage, u32 usageIndex)
{
    if (usage >= std::size(VertexUsageList))
    {
        VERIFY(!"MetalVertexInput::AttributeLocation: unknown D3DDECLUSAGE");
        return ~0u;
    }
    const u32 location = VertexUsageList[usage];
    if (location == ~0u)
        return ~0u; // usage never consumed by the engine's shaders
    return location + usageIndex;
}

MTL::VertexDescriptor* MetalVertexInput::CreateVertexDescriptor(const VertexElement* elements, u32 stride /*= 0*/)
{
    VERIFY(elements);

    MTL::VertexDescriptor* descriptor = MTL::VertexDescriptor::alloc()->init();
    R_ASSERT(descriptor);

    u32 usedStreams = 0;

    for (u32 i = 0; i < MAXD3DDECLLENGTH; ++i)
    {
        const VertexElement& desc = elements[i];
        if (desc.Stream == 0xFF)
            break; // D3DDECL_END

        const u32 location = AttributeLocation(desc.Usage, desc.UsageIndex);
        if (location == ~0u)
            continue; // unsupported usage — skipped like the GL backend

        const MTL::VertexFormat format = TranslateType(desc.Type);
        if (format == MTL::VertexFormatInvalid)
        {
            Msg("! Metal: unsupported vertex element type %u (usage %u)", desc.Type, desc.Usage);
            continue;
        }

        // Vertex streams occupy buffer indices METAL_VERTEX_STREAM_INDEX + N;
        // shader constant buffers live at METAL_CBUFFER_BIND_INDEX (see
        // metalConstantBuffer.h) so the two ranges never collide.
        const u32 bufferIndex = METAL_VERTEX_STREAM_INDEX + desc.Stream;

        MTL::VertexAttributeDescriptor* attribute = descriptor->attributes()->object(location);
        attribute->setFormat(format);
        attribute->setOffset(desc.Offset);
        attribute->setBufferIndex(bufferIndex);

        usedStreams |= 1u << desc.Stream;
    }

    // One interleaved layout per referenced stream.
    for (u32 stream = 0; usedStreams; ++stream, usedStreams >>= 1)
    {
        if (0 == (usedStreams & 1))
            continue;

        const u32 streamStride =
            (0 == stream && stride) ? stride : GetDeclVertexSize(elements, stream);
        VERIFY(streamStride);

        MTL::VertexBufferLayoutDescriptor* layout =
            descriptor->layouts()->object(METAL_VERTEX_STREAM_INDEX + stream);
        layout->setStride(streamStride);
        layout->setStepFunction(MTL::VertexStepFunctionPerVertex);
        layout->setStepRate(1);
    }

    return descriptor;
}

u32 MetalVertexInput::HashVertexDescriptor(const VertexElement* elements)
{
    VERIFY(elements);

    // FNV-1a over the used element fields (order-sensitive).
    u32 hash = 2166136261u;
    const auto mix = [&hash](u32 value)
    {
        hash ^= value;
        hash *= 16777619u;
    };

    for (u32 i = 0; i < MAXD3DDECLLENGTH; ++i)
    {
        const VertexElement& desc = elements[i];
        if (desc.Stream == 0xFF)
            break;

        mix(desc.Stream);
        mix(desc.Offset);
        mix(desc.Type);
        mix(desc.Usage);
        mix(desc.UsageIndex);
    }

    return hash;
}
} // namespace xray::render::RENDER_NAMESPACE
