#include "stdafx.h"

#include "Layers/xrRender/dxRenderFactory.h"
#include "Layers/xrRender/dxUIRender.h"
#include "Layers/xrRender/dxDebugRender.h"
#include "Layers/xrRender/D3DUtils.h"

#include "Include/xrRender/xrRender.h"

namespace xray::render::RENDER_NAMESPACE
{
// Distinct token: GL also serves the R3 pipeline level under "renderer_r3",
// and existing configs must keep resolving to GL. Metal is explicit opt-in
// ("renderer renderer_metal"). This backend is the MAC renderer in the
// dual-native strategy (Vulkan serves Linux/Windows) — see
// specs/2026-07-05-engine-roadmap-v2.md. Becomes default-on at first-frame exit.
constexpr pcstr RENDERER_METAL_MODE = "renderer_metal"; // R3 pipeline level (id 4)

class RMetalRendererModule final : public RendererModule
{
    xr_vector<std::pair<pcstr, int>> modes;

public:
    bool CheckCanAddMode() const
    {
        // don't duplicate
        if (!modes.empty())
        {
            return false;
        }
        return xrRender_test_hw();
    }

    const xr_vector<std::pair<pcstr, int>>& ObtainSupportedModes() override
    {
        ZoneScoped;

        if (CheckCanAddMode())
        {
            modes.emplace_back(RENDERER_METAL_MODE, 4);
        }
        return modes;
    }

    bool CheckGameRequirements() override
    {
        // Check if shaders are available
        if (!FS.exist("$game_shaders$", RImplementation.getShaderPath()))
        {
            Log("~ No shaders found for Metal");
            return false;
        }
        return true;
    }

    void SetupEnv(pcstr /*mode*/) override
    {
        ZoneScoped;

        ps_r2_sun_static  = false;
        ps_r2_advanced_pp = true;

        GEnv.Render        = &RImplementation;
        GEnv.RenderFactory = &RenderFactoryImpl;
        GEnv.DU            = &DUImpl;
        GEnv.UIRender      = &UIRenderImpl;
#ifdef DEBUG
        GEnv.DRender = &DebugRenderImpl;
        rdebug_render->Register();
#endif
        xrRender_initconsole();
    }

    void ClearEnv() override
    {
        modes.clear();

        if (GEnv.Render == &RImplementation)
        {
            GEnv.Render        = nullptr;
            GEnv.RenderFactory = nullptr;
            GEnv.DU            = nullptr;
            GEnv.UIRender      = nullptr;
            GEnv.DRender       = nullptr;
#ifdef DEBUG
            rdebug_render->Unregister();
#endif
        }
    }
} static s_metal_module;

RendererModule* GetRendererModule()
{
    return &s_metal_module;
}
} // namespace xray::render::RENDER_NAMESPACE
