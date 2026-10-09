#pragma once

#include "EngineAPI.h"

#include <cstdint>
#include <string>

class IGame_Level;

// Engine lifecycle wrapper around xrSim::ZoneRuntime. The runtime itself is
// pure; this class supplies the engine clock, the live level adapter
// (g_pGameLevel implements xrSim::IAgentZoneLiveAdapter), the provider built
// from the environment, and detaches before the level disappears.
class ENGINE_API CAgentZoneRuntime
{
public:
    static void Initialize();
    static void Destroy();
    static void OnFrame();
    static std::string HandleBridgeVerb(const std::string& verb, const std::string& payload, bool& ok);
    static void NotifyHit(uint16_t targetId, uint16_t sourceId, uint32_t damagePermille);
    static void NotifyDeath(uint16_t targetId, uint16_t sourceId);
    // Called by IGame_Level when the live level stops/destroys: drops the live
    // adapter without touching it (the objects die with the level).
    static void OnLevelGone(IGame_Level* level);
};
