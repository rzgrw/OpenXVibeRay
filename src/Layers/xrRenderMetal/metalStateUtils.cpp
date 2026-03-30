// metalStateUtils.cpp — D3D9 render state enum → Metal enum converters.

#include "stdafx.h"
#include "metalStateUtils.h"

#ifdef USE_METAL

namespace xray::render::RENDER_NAMESPACE
{
namespace metalStateUtils
{

MTL::CompareFunction ToMTLCompareFunction(u32 func)
{
    switch (func)
    {
    case D3DCMP_NEVER:        return MTL::CompareFunctionNever;
    case D3DCMP_LESS:         return MTL::CompareFunctionLess;
    case D3DCMP_EQUAL:        return MTL::CompareFunctionEqual;
    case D3DCMP_LESSEQUAL:    return MTL::CompareFunctionLessEqual;
    case D3DCMP_GREATER:      return MTL::CompareFunctionGreater;
    case D3DCMP_NOTEQUAL:     return MTL::CompareFunctionNotEqual;
    case D3DCMP_GREATEREQUAL: return MTL::CompareFunctionGreaterEqual;
    case D3DCMP_ALWAYS:       return MTL::CompareFunctionAlways;
    default:
        VERIFY(!"ToMTLCompareFunction: unexpected compare function");
        return MTL::CompareFunctionAlways;
    }
}

MTL::BlendFactor ToMTLBlendFactor(u32 blend)
{
    switch (blend)
    {
    case D3DBLEND_ZERO:          return MTL::BlendFactorZero;
    case D3DBLEND_ONE:           return MTL::BlendFactorOne;
    case D3DBLEND_SRCCOLOR:      return MTL::BlendFactorSourceColor;
    case D3DBLEND_INVSRCCOLOR:   return MTL::BlendFactorOneMinusSourceColor;
    case D3DBLEND_SRCALPHA:      return MTL::BlendFactorSourceAlpha;
    case D3DBLEND_INVSRCALPHA:   return MTL::BlendFactorOneMinusSourceAlpha;
    case D3DBLEND_DESTALPHA:     return MTL::BlendFactorDestinationAlpha;
    case D3DBLEND_INVDESTALPHA:  return MTL::BlendFactorOneMinusDestinationAlpha;
    case D3DBLEND_DESTCOLOR:     return MTL::BlendFactorDestinationColor;
    case D3DBLEND_INVDESTCOLOR:  return MTL::BlendFactorOneMinusDestinationColor;
    case D3DBLEND_SRCALPHASAT:   return MTL::BlendFactorSourceAlphaSaturated;
    case D3DBLEND_BLENDFACTOR:   return MTL::BlendFactorBlendColor;
    case D3DBLEND_INVBLENDFACTOR: return MTL::BlendFactorOneMinusBlendColor;
    default:
        VERIFY(!"ToMTLBlendFactor: unexpected blend factor");
        return MTL::BlendFactorOne;
    }
}

MTL::BlendOperation ToMTLBlendOperation(u32 op)
{
    switch (op)
    {
    case D3DBLENDOP_ADD:         return MTL::BlendOperationAdd;
    case D3DBLENDOP_SUBTRACT:    return MTL::BlendOperationSubtract;
    case D3DBLENDOP_REVSUBTRACT: return MTL::BlendOperationReverseSubtract;
    case D3DBLENDOP_MIN:         return MTL::BlendOperationMin;
    case D3DBLENDOP_MAX:         return MTL::BlendOperationMax;
    default:
        VERIFY(!"ToMTLBlendOperation: unexpected blend operation");
        return MTL::BlendOperationAdd;
    }
}

MTL::StencilOperation ToMTLStencilOperation(u32 op)
{
    switch (op)
    {
    case D3DSTENCILOP_KEEP:    return MTL::StencilOperationKeep;
    case D3DSTENCILOP_ZERO:    return MTL::StencilOperationZero;
    case D3DSTENCILOP_REPLACE: return MTL::StencilOperationReplace;
    case D3DSTENCILOP_INCRSAT: return MTL::StencilOperationIncrementClamp;
    case D3DSTENCILOP_DECRSAT: return MTL::StencilOperationDecrementClamp;
    case D3DSTENCILOP_INVERT:  return MTL::StencilOperationInvert;
    case D3DSTENCILOP_INCR:    return MTL::StencilOperationIncrementWrap;
    case D3DSTENCILOP_DECR:    return MTL::StencilOperationDecrementWrap;
    default:
        VERIFY(!"ToMTLStencilOperation: unexpected stencil operation");
        return MTL::StencilOperationKeep;
    }
}

MTL::CullMode ToMTLCullMode(u32 d3dCull)
{
    switch (d3dCull)
    {
    case D3DCULL_NONE: return MTL::CullModeNone;
    case D3DCULL_CW:   return MTL::CullModeBack;
    case D3DCULL_CCW:  return MTL::CullModeFront;
    default:
        VERIFY(!"ToMTLCullMode: unexpected cull mode");
        return MTL::CullModeNone;
    }
}

MTL::TriangleFillMode ToMTLFillMode(u32 d3dFill)
{
    switch (d3dFill)
    {
    case D3DFILL_SOLID:     return MTL::TriangleFillModeFill;
    case D3DFILL_WIREFRAME: return MTL::TriangleFillModeLines;
    // D3DFILL_POINT has no direct Metal equivalent; fall back to lines
    case D3DFILL_POINT:     return MTL::TriangleFillModeLines;
    default:
        VERIFY(!"ToMTLFillMode: unexpected fill mode");
        return MTL::TriangleFillModeFill;
    }
}

} // namespace metalStateUtils
} // namespace xray::render::RENDER_NAMESPACE

#endif // USE_METAL
