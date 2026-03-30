#pragma once

// TODO: Get rid of D3D types.
#if defined(XR_PLATFORM_WINDOWS)
#include <d3d9types.h>
#else
#include "Common/d3d9compat.hpp"
#endif

// metal-cpp C++ wrappers for Metal types.
// Do NOT define METAL_IMPLEMENTATION here — that must be done exactly once
// in a single .cpp translation unit (e.g. metalHW.cpp).
#if defined(USE_METAL)
#include <Metal/Metal.hpp>
#endif

namespace xray::render::RENDER_NAMESPACE
{
class metalState;

typedef enum D3D_CLEAR_FLAG {
    D3D_CLEAR_DEPTH   = 0x1L,
    D3D_CLEAR_STENCIL = 0x2L
} D3D_CLEAR_FLAG;

// D3D_COMPARISON_FUNC values match MTL::CompareFunction exactly so that
// the shared render code can cast them directly to Metal compare functions.
// MTL::CompareFunction: Never=0, Less=1, Equal=2, LessEqual=3,
//                       Greater=4, NotEqual=5, GreaterEqual=6, Always=7.
typedef enum D3D_COMPARISON_FUNC {
#if defined(USE_METAL)
    D3D_COMPARISON_NEVER         = MTL::CompareFunctionNever,
    D3D_COMPARISON_LESS          = MTL::CompareFunctionLess,
    D3D_COMPARISON_EQUAL         = MTL::CompareFunctionEqual,
    D3D_COMPARISON_LESS_EQUAL    = MTL::CompareFunctionLessEqual,
    D3D_COMPARISON_GREATER       = MTL::CompareFunctionGreater,
    D3D_COMPARISON_NOT_EQUAL     = MTL::CompareFunctionNotEqual,
    D3D_COMPARISON_GREATER_EQUAL = MTL::CompareFunctionGreaterEqual,
    D3D_COMPARISON_ALWAYS        = MTL::CompareFunctionAlways,
#else
    // Fallback numeric values matching MTL::CompareFunction for non-Metal builds.
    D3D_COMPARISON_NEVER         = 0,
    D3D_COMPARISON_LESS          = 1,
    D3D_COMPARISON_EQUAL         = 2,
    D3D_COMPARISON_LESS_EQUAL    = 3,
    D3D_COMPARISON_GREATER       = 4,
    D3D_COMPARISON_NOT_EQUAL     = 5,
    D3D_COMPARISON_GREATER_EQUAL = 6,
    D3D_COMPARISON_ALWAYS        = 7,
#endif
} D3D_COMPARISON_FUNC;

// Viewport struct — float coordinates matching the MTLViewport layout.
struct XR_METAL_VIEWPORT
{
    float TopLeftX, TopLeftY;
    float Width, Height;
    float MinDepth, MaxDepth;
};

struct D3D_VIEWPORT : XR_METAL_VIEWPORT
{
    using XR_METAL_VIEWPORT::XR_METAL_VIEWPORT;

    template <typename TopLeftCoords, typename Dimensions>
    D3D_VIEWPORT(TopLeftCoords x, TopLeftCoords y, Dimensions w, Dimensions h, float minZ, float maxZ)
        : XR_METAL_VIEWPORT{
            static_cast<float>(x), static_cast<float>(y),
            static_cast<float>(w), static_cast<float>(h),
            minZ, maxZ,
          }
    {}
};

using D3D_QUERY = enum XR_METAL_QUERY
{
    D3D_QUERY_EVENT,
    D3D_QUERY_OCCLUSION
};

// D3D-style depth/stencil and blend state descriptors.
// Used by metalState and shared render code.
typedef struct
{
    BOOL        DepthEnable;
    BOOL        DepthWriteMask;
    D3DCMPFUNC  DepthFunc;
    BOOL        StencilEnable;
    u32         StencilMask;
    u32         StencilWriteMask;
    D3DSTENCILOP StencilFailOp;
    D3DSTENCILOP StencilDepthFailOp;
    D3DSTENCILOP StencilPassOp;
    D3DCMPFUNC  StencilFunc;
    u32         StencilRef;
} D3D_DEPTH_STENCIL_STATE;

typedef struct
{
    BOOL        BlendEnable;
    D3DBLEND    SrcBlend;
    D3DBLEND    DestBlend;
    D3DBLENDOP  BlendOp;
    D3DBLEND    SrcBlendAlpha;
    D3DBLEND    DestBlendAlpha;
    D3DBLENDOP  BlendOpAlpha;
    u32         ColorMask;
} D3D_BLEND_STATE;

using ID3DState = metalState;

#define DX11_ONLY(expr) do {} while (0)

using unused_t = int[0];

// Buffer handles — MTL::Buffer* for Metal, void* fallback for non-Metal builds.
#if defined(USE_METAL)
using IndexBufferHandle    = MTL::Buffer*;
using VertexBufferHandle   = MTL::Buffer*;
using ConstantBufferHandle = MTL::Buffer*;
#else
using IndexBufferHandle    = void*;
using VertexBufferHandle   = void*;
using ConstantBufferHandle = void*;
#endif
using HostBufferHandle     = void*;

using VertexElement    = D3DVERTEXELEMENT9;
using InputElementDesc = unused_t;

} // namespace xray::render::RENDER_NAMESPACE
