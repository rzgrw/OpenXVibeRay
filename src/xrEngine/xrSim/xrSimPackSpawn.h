#pragma once

#include "xrSim/xrSimActorRuntime.h"

#include <cstdint>
#include <string>
#include <vector>

namespace xrSim
{
struct PackSpawnRequest
{
    std::string section;
    uint32_t count = 0;
    uint32_t radiusMeters = 8;
};

struct PackSpawnParseResult
{
    bool ok = false;
    std::string reason;
    PackSpawnRequest request;
};

struct PackRegistration
{
    std::string section;
    std::vector<uint16_t> objectIds;
    std::string spawnReason;
};

struct PackRegisterResult
{
    bool ok = false;
    uint32_t packId = 0;
    std::string reason;
};

struct PackWakePrepareResult
{
    bool ok = false;
    std::string reason;
    ActorWakeContext context;
};

PackSpawnParseResult ParsePackSpawnPayload(const std::string& payload);
PackRegisterResult RegisterSessionMutantPack(const PackRegistration& registration);
void ResetSessionPacks();
void ResetSessionPacksForTests();
std::string FormatPackList();
std::string ObservePack(uint32_t packId, const WorldState& world);
PackWakePrepareResult PreparePackWake(uint32_t packId, const WorldState& world, uint32_t gameDay);
ActuatorResult ApplyPackWakeResult(uint32_t packId, WorldState& world, ActorRuntime& runtime,
    const ActorProviderResult& providerResult, uint32_t gameDay);
ActuatorResult WakePack(uint32_t packId, WorldState& world, ActorRuntime& runtime, uint32_t gameDay);
} // namespace xrSim
