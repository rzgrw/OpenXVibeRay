#include "AgentZoneRuntime.h"
#include "xrSim/xrSimZoneRuntime.h"
#include "device.h"

#include <memory>

namespace
{
std::unique_ptr<xrSim::ZoneRuntime> g_zoneRuntime;
} // namespace

void CAgentZoneRuntime::Initialize()
{
    if (!g_zoneRuntime)
        g_zoneRuntime = std::make_unique<xrSim::ZoneRuntime>();
}

void CAgentZoneRuntime::Destroy()
{
    if (g_zoneRuntime)
        g_zoneRuntime->Reset();
    g_zoneRuntime.reset();
}

void CAgentZoneRuntime::OnFrame()
{
    if (g_zoneRuntime)
    {
        const uint64_t nowMs = static_cast<uint64_t>(Device.dwTimeGlobal);
        const uint64_t frame = static_cast<uint64_t>(Device.dwFrame);
        g_zoneRuntime->OnFrame(nowMs, frame);
    }
}

std::string CAgentZoneRuntime::HandleBridgeVerb(
    const std::string& verb, const std::string& payload, bool& ok)
{
    if (g_zoneRuntime)
        return g_zoneRuntime->HandleBridgeVerb(verb, payload, ok);
    ok = false;
    return "zone_runtime_not_initialized";
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
