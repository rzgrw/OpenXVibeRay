#pragma once

// metal-cpp must come FIRST: it pulls <objc/objc.h>, whose `typedef bool BOOL`
// conflicts with the engine's `typedef int32_t BOOL` (PlatformApple.inl guards
// its typedef on OBJC_BOOL_DEFINED, so Metal TUs use the ObjC definition).
// The ObjC convenience macros would break luabind (`nil`) and engine code —
// drop them; metal-cpp itself never uses them after this point.
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
#undef nil
#undef Nil
#undef YES
#undef NO

#include "xrEngine/stdafx.h"

#define R_GL 0
#define R_R1 1
#define R_R2 2
#define R_R3 3
#define R_R4 4
#define RENDER R_GL
#define USE_METAL

#include "xrEngine/vis_common.h"
#include "xrEngine/Render.h"
#include "xrEngine/IGame_Level.h"

#include "xrParticles/psystem.h"

#include "Layers/xrRenderMetal/CommonTypes.h"

#include "Layers/xrRenderMetal/metalHW.h"
#include "Layers/xrRender/Debug/dxPixEventWrapper.h"

#include "Layers/xrRender/Shader.h"

#include "Layers/xrRender/R_Backend.h"
#include "Layers/xrRender/R_Backend_Runtime.h"

#include "Layers/xrRender/Blender.h"
#include "Layers/xrRender/Blender_CLSID.h"

#include "Common/_d3d_extensions.h"

#include "Layers/xrRender/ResourceManager.h"
#include "Layers/xrRender/xrRender_console.h"

#include "r2.h"
#include "metal_rendertarget.h"

namespace xray::render::RENDER_NAMESPACE
{
IC void jitter(CBlender_Compile& C)
{
    C.r_Sampler("jitter0", JITTER(0), true, D3DTADDRESS_WRAP, D3DTEXF_POINT, D3DTEXF_NONE, D3DTEXF_POINT);
    C.r_Sampler("jitter1", JITTER(1), true, D3DTADDRESS_WRAP, D3DTEXF_POINT, D3DTEXF_NONE, D3DTEXF_POINT);
    C.r_Sampler("jitter2", JITTER(2), true, D3DTADDRESS_WRAP, D3DTEXF_POINT, D3DTEXF_NONE, D3DTEXF_POINT);
    C.r_Sampler("jitter3", JITTER(3), true, D3DTADDRESS_WRAP, D3DTEXF_POINT, D3DTEXF_NONE, D3DTEXF_POINT);
}
} // namespace xray::render::RENDER_NAMESPACE
