#pragma once

#include "EngineAPI.h"

#include <cstdint>
#include <string>

class ENGINE_API CAgentZoneRuntime
{
public:
    static void Initialize();
    static void Destroy();
    static void OnFrame();
    static std::string HandleBridgeVerb(const std::string& verb, const std::string& payload, bool& ok);
    static void NotifyHit(uint16_t targetId, uint16_t sourceId, uint32_t damagePermille);
    static void NotifyDeath(uint16_t targetId, uint16_t sourceId);
};
