// metalHWCaps.cpp — fill CHWCaps for the Metal backend.
// Mirrors glHWCaps.cpp; capability values that Metal cannot query directly
// are derived from the MTL::Device GPU-family support checks.

#include "stdafx.h"
#pragma hdrstop

#include "Layers/xrRender/HWCaps.h"
#include "metalHW.h"

namespace xray::render::RENDER_NAMESPACE
{
namespace
{
u32 GetGpuNum()
{
    // Like the GL backend, report 2 to keep the double-buffered
    // occlusion-query / luminance-history pipelining intact.
    return 2;
}
}

void CHWCaps::Update()
{
    MTL::Device* device = HW.pDevice;

    // ***************** GEOMETRY
    geometry_major = 4;
    geometry_minor = 0;
    geometry_profile = "vs_4_0";
    geometry.bSoftware = FALSE;
    geometry.bPointSprites = FALSE;
    geometry.bNPatches = FALSE;
    u32 cnt = 256;
    clamp<u32>(cnt, 0, 256);
    geometry.dwRegisters = cnt;
    geometry.dwInstructions = 256;
    geometry.dwClipPlanes = _min(6, 15);
    // Vertex texture fetch is universally supported on Metal
    geometry.bVTF = !strstr(Core.Params, "-novtf");

    // ***************** PIXEL processing
    raster_major = 4;
    raster_minor = 0;
    raster_profile = "ps_4_0";
    raster.dwStages = 15;
    raster.bNonPow2 = TRUE;
    raster.bCubemap = TRUE;
    // Metal guarantees at least 8 color attachments; the engine uses 4
    raster.dwMRT_count = 4;
    raster.b_MRT_mixdepth = TRUE;
    raster.dwInstructions = 256;
    //	TODO: Metal: Find a way to detect cache size
    geometry.dwVertexCache = 24;

    // *******1********** Compatibility : vertex shader
    if (0 == raster_major)
        geometry_major = 0; // Disable VS if no PS

    //
    bTableFog = FALSE;

    // Detect if stencil available
    bStencil = TRUE;

    // Scissoring
    bScissor = TRUE;

    // Stencil relative caps
    soInc = D3DSTENCILOP_INCRSAT;
    soDec = D3DSTENCILOP_DECRSAT;
    dwMaxStencilValue = (1 << 8) - 1;

    // FFP lights
    max_ffp_lights = 0;

    // DEV INFO

    iGPUNum = GetGpuNum();

    // Shaders arrive through the GL(SL) pipeline (SPIRV-Cross splits
    // combined samplers into MSL texture+sampler pairs automatically),
    // so from the engine's perspective samplers stay combined.
    useCombinedSamplers = true;

    id_vendor = 0x106B; // Apple
    id_device = 0;

    if (device)
    {
        const bool apple_gpu = device->supportsFamily(MTL::GPUFamilyApple1);
        const u64 working_set_mib = device->recommendedMaxWorkingSetSize() / (1024ull * 1024ull);

        Msg("* Metal: GPU [%s]%s", device->name()->utf8String(),
            apple_gpu ? " (Apple silicon)" : "");
        Msg("* Metal: unified memory: %s, recommended working set: %llu MiB",
            device->hasUnifiedMemory() ? "yes" : "no", working_set_mib);

        // Newest-to-oldest so the log shows the highest supported family
        if (device->supportsFamily(MTL::GPUFamilyApple9))
            Msg("* Metal: GPU family Apple9");
        else if (device->supportsFamily(MTL::GPUFamilyApple8))
            Msg("* Metal: GPU family Apple8");
        else if (device->supportsFamily(MTL::GPUFamilyApple7))
            Msg("* Metal: GPU family Apple7");
        else if (device->supportsFamily(MTL::GPUFamilyApple6))
            Msg("* Metal: GPU family Apple6");
        else if (device->supportsFamily(MTL::GPUFamilyMac2))
            Msg("* Metal: GPU family Mac2");
    }
    else
        Msg("~ Metal: CHWCaps::Update() called without a device — using defaults");
}
} // namespace xray::render::RENDER_NAMESPACE
