// metal_rendertarget.cpp — stub implementations for CRenderTarget (Metal backend).
//
// All method bodies are empty stubs. Full implementation will be added in Task 17
// once the Metal pipeline, command encoder, and render passes are wired up.

#include "stdafx.h"

namespace xray::render::RENDER_NAMESPACE
{

CRenderTarget::CRenderTarget()  {}
CRenderTarget::~CRenderTarget() {}

void CRenderTarget::build_textures() {}

void CRenderTarget::accum_point_geom_create()      {}
void CRenderTarget::accum_point_geom_destroy()     {}
void CRenderTarget::accum_omnip_geom_create()      {}
void CRenderTarget::accum_omnip_geom_destroy()     {}
void CRenderTarget::accum_spot_geom_create()       {}
void CRenderTarget::accum_spot_geom_destroy()      {}
void CRenderTarget::accum_volumetric_geom_create() {}
void CRenderTarget::accum_volumetric_geom_destroy(){}

void CRenderTarget::u_setrt(CBackend& /*cmd_list*/, const ref_rt& /*_1*/, const ref_rt& /*_2*/, const ref_rt& /*_3*/, const ref_rt& /*_zb*/) {}
void CRenderTarget::u_setrt(CBackend& /*cmd_list*/, const ref_rt& /*_1*/, const ref_rt& /*_2*/, const ref_rt& /*_zb*/) {}
void CRenderTarget::u_setrt(CBackend& /*cmd_list*/, u32 /*W*/, u32 /*H*/, void* /*_1*/, void* /*_2*/, void* /*_3*/, void* /*zb*/) {}

void CRenderTarget::u_stencil_optimize(CBackend& /*cmd_list*/, eStencilOptimizeMode /*eSOM*/) {}
void CRenderTarget::u_compute_texgen_screen(CBackend& /*cmd_list*/, Fmatrix& /*dest*/) {}
void CRenderTarget::u_compute_texgen_jitter(CBackend& /*cmd_list*/, Fmatrix& /*dest*/) {}
void CRenderTarget::u_calc_tc_noise(Fvector2& /*p0*/, Fvector2& /*p1*/) {}
void CRenderTarget::u_calc_tc_duality_ss(Fvector2& /*r0*/, Fvector2& /*r1*/, Fvector2& /*l0*/, Fvector2& /*l1*/) {}
bool CRenderTarget::u_need_PP()                              { return false; }
bool CRenderTarget::u_need_CM()                              { return false; }
bool CRenderTarget::u_DBT_enable(float /*zMin*/, float /*zMax*/) { return false; }
void CRenderTarget::u_DBT_disable() {}

void CRenderTarget::phase_scene_prepare() {}
void CRenderTarget::phase_scene_begin()   {}
void CRenderTarget::phase_scene_end()     {}
void CRenderTarget::phase_occq()          {}
void CRenderTarget::phase_ssao()          {}
void CRenderTarget::phase_downsamp()      {}
void CRenderTarget::phase_wallmarks()     {}

void CRenderTarget::phase_smap_direct(CBackend& /*cmd_list*/, light* /*L*/, u32 /*sub_phase*/)     {}
void CRenderTarget::phase_smap_direct_tsh(CBackend& /*cmd_list*/, light* /*L*/, u32 /*sub_phase*/) {}
void CRenderTarget::phase_smap_spot_clear(CBackend& /*cmd_list*/)                                  {}
void CRenderTarget::phase_smap_spot(CBackend& /*cmd_list*/, light* /*L*/)                          {}
void CRenderTarget::phase_smap_spot_tsh(CBackend& /*cmd_list*/, light* /*L*/)                      {}
void CRenderTarget::phase_accumulator(CBackend& /*cmd_list*/)                                       {}
void CRenderTarget::phase_vol_accumulator(CBackend& /*cmd_list*/)                                   {}
void CRenderTarget::shadow_direct(CBackend& /*cmd_list*/, light* /*L*/, u32 /*dls_phase*/)         {}

void CRenderTarget::create_minmax_SM(CBackend& /*cmd_list*/) {}

void CRenderTarget::phase_rain(CBackend& /*cmd_list*/)                  {}
void CRenderTarget::draw_rain(CBackend& /*cmd_list*/, light& /*RainSetup*/) {}

void CRenderTarget::mark_msaa_edges() {}

bool CRenderTarget::need_to_render_sunshafts()  { return false; }
bool CRenderTarget::use_minmax_sm_this_frame()   { return false; }

bool CRenderTarget::enable_scissor(light* /*L*/)   { return false; }
void CRenderTarget::enable_dbt_bounds(light* /*L*/) {}

void CRenderTarget::disable_aniso() {}

void CRenderTarget::draw_volume(CBackend& /*cmd_list*/, light* /*L*/)                                           {}
void CRenderTarget::accum_direct(CBackend& /*cmd_list*/, u32 /*sub_phase*/)                                     {}
void CRenderTarget::accum_direct_cascade(CBackend& /*cmd_list*/, u32 /*sub_phase*/, Fmatrix& /*xform*/, Fmatrix& /*xform_prev*/, float /*fBias*/) {}
void CRenderTarget::accum_direct_f(CBackend& /*cmd_list*/, u32 /*sub_phase*/)                                   {}
void CRenderTarget::accum_direct_lum(CBackend& /*cmd_list*/)                                                    {}
void CRenderTarget::accum_direct_blend(CBackend& /*cmd_list*/)                                                  {}
void CRenderTarget::accum_direct_volumetric(u32 /*sub_phase*/, const u32 /*Offset*/, const Fmatrix& /*mShadow*/) {}
void CRenderTarget::accum_point(CBackend& /*cmd_list*/, light* /*L*/)                                           {}
void CRenderTarget::accum_spot(CBackend& /*cmd_list*/, light* /*L*/)                                            {}
void CRenderTarget::accum_reflected(CBackend& /*cmd_list*/, light* /*L*/)                                       {}
void CRenderTarget::accum_volumetric(CBackend& /*cmd_list*/, light* /*L*/)                                      {}

void CRenderTarget::phase_bloom()             {}
void CRenderTarget::phase_luminance()         {}
void CRenderTarget::phase_combine()           {}
void CRenderTarget::phase_combine_volumetric() {}
void CRenderTarget::phase_pp()                {}

void CRenderTarget::reset_light_marker(CBackend& /*cmd_list*/, bool /*bResetStencil*/) {}
void CRenderTarget::increment_light_marker(CBackend& /*cmd_list*/)                     {}

} // namespace xray::render::RENDER_NAMESPACE
