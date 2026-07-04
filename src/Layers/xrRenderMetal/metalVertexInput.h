#pragma once

// metalVertexInput.h — translation of X-Ray vertex declarations
// (D3DVERTEXELEMENT9 arrays) into MTL::VertexDescriptor objects.
//
// Metal requires the vertex layout at pipeline (PSO) creation time, so
// CResourceManager::_CreateDecl converts the declaration once (via
// ConvertVertexDeclaration in metalBufferUtils.cpp, which stores the
// descriptor in SDeclaration::dcl as a uint64_t handle) and the PSO cache
// (plan Task 16/19) keys on HashVertexDescriptor.

#include "Layers/xrRenderMetal/CommonTypes.h"

namespace xray::render::RENDER_NAMESPACE
{
class MetalVertexInput
{
public:
    // Convert an X-Ray vertex declaration to a Metal vertex descriptor.
    // The returned descriptor is created with alloc()->init() — the caller
    // owns it and must release() it (SDeclaration teardown, plan Task 14).
    //
    // stride: optional stream-0 stride override; pass 0 to compute the
    // stride from the declaration (GetDeclVertexSize).
    static MTL::VertexDescriptor* CreateVertexDescriptor(const VertexElement* elements, u32 stride = 0);

    // Order-sensitive FNV-1a hash over the used declaration elements —
    // stable PSO cache key component for a vertex layout.
    static u32 HashVertexDescriptor(const VertexElement* elements);

    // D3DDECLTYPE -> MTL::VertexFormat (MTL::VertexFormatInvalid if unmappable).
    static MTL::VertexFormat TranslateType(u32 type);

    // D3DDECLUSAGE + UsageIndex -> MSL [[attribute(n)]] slot.
    // Returns u32(-1) for usages the engine's shaders never consume.
    //
    // NOTE: this table mirrors the GL backend's VertexUsageList (see
    // glBufferUtils.cpp) — COLOR=0, POSITION=3, TANGENT=4, NORMAL=5,
    // BINORMAL=6, FOG=7, TEXCOORD=8+index.  The Metal shader pipeline
    // (Task 16) MUST assign the same attribute locations when compiling
    // vertex shaders (SPIRV-Cross MSL stage-in attributes), or vertex
    // fetch will be silently wrong.
    static u32 AttributeLocation(u32 usage, u32 usageIndex);
};
} // namespace xray::render::RENDER_NAMESPACE
