// metal_rendertarget.cpp — Metal-specific CRenderTarget pieces.
//
// The bulk of CRenderTarget comes from the shared xrRender_R2 sources
// (r2_rendertarget.cpp provides the constructor/destructor and utility
// helpers, r2_/r3_rendertarget_*.cpp provide the API-agnostic phases) —
// exactly like the GL backend.  The Metal-specific implementation lives in:
//  - metal_rendertarget_build_textures.cpp (material/noise textures)
//  - metal_rendertarget_u_set_rt.cpp       (u_setrt overload family)
//
// This file only holds temporary stubs for the phases that GL implements in
// gl_rendertarget_accum_direct.cpp and gl_rendertarget_phase_combine.cpp.
// TODO Task 18: replace these stubs with metal_rendertarget_accum_direct.cpp
// and metal_rendertarget_phase_combine.cpp, then delete this file.

#include "stdafx.h"

namespace xray::render::RENDER_NAMESPACE
{
// TODO Task 18: metal_rendertarget_accum_direct.cpp (sun/direct light accumulation)
void CRenderTarget::accum_direct(CBackend& /*cmd_list*/, u32 /*sub_phase*/) {}
void CRenderTarget::accum_direct_cascade(
    CBackend& /*cmd_list*/, u32 /*sub_phase*/, Fmatrix& /*xform*/, Fmatrix& /*xform_prev*/, float /*fBias*/) {}
void CRenderTarget::accum_direct_f(CBackend& /*cmd_list*/, u32 /*sub_phase*/) {}
void CRenderTarget::accum_direct_lum(CBackend& /*cmd_list*/) {}
void CRenderTarget::accum_direct_blend(CBackend& /*cmd_list*/) {}
void CRenderTarget::accum_direct_volumetric(u32 /*sub_phase*/, const u32 /*Offset*/, const Fmatrix& /*mShadow*/) {}

// TODO Task 18: metal_rendertarget_phase_combine.cpp (final combine + wallmarks)
void CRenderTarget::phase_combine() {}
void CRenderTarget::phase_combine_volumetric() {}
void CRenderTarget::phase_wallmarks() {}
} // namespace xray::render::RENDER_NAMESPACE
