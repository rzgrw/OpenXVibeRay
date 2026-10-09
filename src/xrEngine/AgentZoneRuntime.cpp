#include "AgentZoneRuntime.h"
#include "xrSim/xrSimZoneRuntime.h"
#include "xrSim/xrSimAgentProvider.h"
#include "xrSim/xrSimWorldProvider.h"
#include "device.h"
#include "IGame_Level.h"

#include <memory>

namespace
{
std::unique_ptr<xrSim::ZoneRuntime> g_zoneRuntime;
IGame_Level* g_zoneAttachedLevel = nullptr;
std::string g_zoneProviderDescription;

void DetachZoneLevel(const char* why)
{
    if (!g_zoneRuntime || !g_zoneAttachedLevel)
        return;
    Msg("* [agent.zone] detaching live adapter (%s): ticks=%llu bound=%zu", why,
        static_cast<unsigned long long>(g_zoneRuntime->Metrics().simulationTicks),
        g_zoneRuntime->LiveMaterializer() ? g_zoneRuntime->LiveMaterializer()->Bindings().size() : size_t(0));
    // The level owns the spawned objects; never call back into it from here.
    g_zoneRuntime->DetachLive(false);
    g_zoneAttachedLevel = nullptr;
    g_zoneProviderDescription.clear();
}

std::string VerbZoneStart(const std::string& payload, bool& ok)
{
    if (!g_zoneRuntime->IsBootstrapped())
        return g_zoneRuntime->HandleBridgeVerb("agent.zone.start", payload, ok);

    if (!g_zoneRuntime->IsAttached())
    {
        if (!g_pGameLevel || !g_pGameLevel->bReady)
        {
            ok = false;
            return "level_not_ready";
        }

        // "" / "live": provider from the environment (coasts without a key).
        // "null": deterministic coast provider regardless of the environment.
        xrSim::AgentProviderConfig providerConfig = xrSim::LoadAgentProviderConfigFromEnvironment();
        if (payload == "null")
        {
            providerConfig.enabled = false;
            providerConfig.apiKey.clear();
            providerConfig.provider = "null";
            providerConfig.model = "deterministic-null";
        }
        else if (!payload.empty() && payload != "live")
        {
            ok = false;
            return "usage: agent.zone.start [live|null]";
        }

        xrSim::ZoneRuntimeConfig config;
        std::unique_ptr<xrSim::IWorldIntentProvider> provider =
            xrSim::CreateLiveWorldIntentProvider(providerConfig);
        const xrSim::ZoneRuntimeResult attached =
            g_zoneRuntime->AttachLive(*g_pGameLevel, config, std::move(provider));
        if (!attached.ok)
        {
            ok = false;
            return "attach_failed: " + attached.reason;
        }
        g_zoneAttachedLevel = g_pGameLevel;
        g_zoneProviderDescription = xrSim::DescribeAgentProviderConfig(providerConfig);
        Msg("* [agent.zone] attached live adapter level=%s scenario=%s %s", g_pGameLevel->name().c_str(),
            g_zoneRuntime->Manifest().scenario.c_str(), g_zoneProviderDescription.c_str());
    }

    std::string result = g_zoneRuntime->HandleBridgeVerb("agent.zone.start", payload, ok);
    if (ok)
        result += " " + g_zoneProviderDescription;
    return result;
}
} // namespace

void CAgentZoneRuntime::Initialize()
{
    if (!g_zoneRuntime)
        g_zoneRuntime = std::make_unique<xrSim::ZoneRuntime>();
}

void CAgentZoneRuntime::Destroy()
{
    DetachZoneLevel("shutdown");
    if (g_zoneRuntime)
        g_zoneRuntime->Reset();
    g_zoneRuntime.reset();
}

void CAgentZoneRuntime::OnFrame()
{
    if (!g_zoneRuntime)
        return;

    // Safety net: the attached level must still be the current, ready level.
    if (g_zoneAttachedLevel && (g_pGameLevel != g_zoneAttachedLevel || !g_pGameLevel->bReady))
        DetachZoneLevel("level changed");

    const uint64_t nowMs = static_cast<uint64_t>(Device.dwTimeGlobal);
    const uint64_t frame = static_cast<uint64_t>(Device.dwFrame);
    g_zoneRuntime->OnFrame(nowMs, frame);
}

std::string CAgentZoneRuntime::HandleBridgeVerb(
    const std::string& verb, const std::string& payload, bool& ok)
{
    if (!g_zoneRuntime)
    {
        ok = false;
        return "zone_runtime_not_initialized";
    }
    if (verb == "agent.zone.start")
        return VerbZoneStart(payload, ok);
    if (verb == "agent.zone.reset")
    {
        // The level is still alive here, so let the runtime release what it spawned.
        g_zoneAttachedLevel = nullptr;
        g_zoneProviderDescription.clear();
    }
    return g_zoneRuntime->HandleBridgeVerb(verb, payload, ok);
}

void CAgentZoneRuntime::NotifyHit(uint16_t targetId, uint16_t sourceId, uint32_t damagePermille)
{
    if (g_zoneRuntime)
        g_zoneRuntime->NotifyHit(targetId, sourceId, damagePermille);
}

void CAgentZoneRuntime::NotifyDeath(uint16_t targetId, uint16_t sourceId)
{
    if (g_zoneRuntime)
        g_zoneRuntime->NotifyDeath(targetId, sourceId);
}

void CAgentZoneRuntime::OnLevelGone(IGame_Level* level)
{
    if (level && level == g_zoneAttachedLevel)
        DetachZoneLevel("level stopped");
}
