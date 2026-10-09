#include "xrSim/xrSimPackSpawn.h"

#include <sstream>

namespace xrSim
{
namespace
{
struct SessionPack
{
    uint32_t packId = 0;
    std::string section;
    std::vector<uint16_t> objectIds;
    ActorAgentRecord actor;
};

std::vector<SessionPack> g_sessionPacks;
uint32_t g_nextPackId = 1;

PackSpawnParseResult FailParse(const std::string& reason)
{
    PackSpawnParseResult result;
    result.reason = reason;
    return result;
}

PackRegisterResult FailRegister(const std::string& reason)
{
    PackRegisterResult result;
    result.reason = reason;
    return result;
}

SessionPack* FindPack(uint32_t packId)
{
    for (SessionPack& pack : g_sessionPacks)
    {
        if (pack.packId == packId)
            return &pack;
    }
    return nullptr;
}

std::string FormatObjectIds(const std::vector<uint16_t>& ids)
{
    std::ostringstream out;
    for (size_t i = 0; i < ids.size(); ++i)
    {
        if (i != 0)
            out << ",";
        out << ids[i];
    }
    return out.str();
}

std::string BuildPackSituation(const SessionPack& pack)
{
    std::ostringstream out;
    out << "session_spawned members=" << pack.objectIds.size() << " ids=" << FormatObjectIds(pack.objectIds);
    return out.str();
}
} // namespace

PackSpawnParseResult ParsePackSpawnPayload(const std::string& payload)
{
    std::istringstream in(payload);
    PackSpawnRequest request;
    if (!(in >> request.section >> request.count))
        return FailParse("usage: agent.pack.spawn <section> <count> [radius_m]");

    if (request.section.empty())
        return FailParse("missing section");

    if (!(in >> request.radiusMeters))
        request.radiusMeters = 8;

    std::string trailing;
    if (in >> trailing)
        return FailParse("unexpected trailing token: " + trailing);

    if (request.count < 1 || request.count > 8)
        return FailParse("count must be in 1..8");

    if (request.radiusMeters < 1 || request.radiusMeters > 40)
        return FailParse("radius_m must be in 1..40");

    PackSpawnParseResult result;
    result.ok = true;
    result.request = request;
    return result;
}

PackRegisterResult RegisterSessionMutantPack(const PackRegistration& registration)
{
    if (registration.section.empty())
        return FailRegister("missing section");

    if (registration.objectIds.empty())
        return FailRegister("missing pack members");

    if (registration.objectIds.size() > 8)
        return FailRegister("too many pack members");

    const uint32_t packId = g_nextPackId++;
    SessionPack pack;
    pack.packId = packId;
    pack.section = registration.section;
    pack.objectIds = registration.objectIds;

    const bool isSquad = registration.section.find("stalker") != std::string::npos ||
        registration.section.find("bandit") != std::string::npos ||
        registration.section.find("duty") != std::string::npos ||
        registration.section.find("freedom") != std::string::npos ||
        registration.section.find("monolith") != std::string::npos ||
        registration.section.find("soldier") != std::string::npos ||
        registration.section.find("army") != std::string::npos ||
        registration.section.find("csky") != std::string::npos ||
        registration.section.find("killer") != std::string::npos ||
        registration.section.find("ecolog") != std::string::npos;

    const bool isDuty = registration.section.find("duty") != std::string::npos;
    pack.actor.scope = isSquad ? ActorAgentScope::Squad : ActorAgentScope::MutantPack;
    pack.actor.agentId = (isSquad ? 1000 : 2000) + packId;
    pack.actor.name = "session_" + registration.section + (isSquad ? "_squad_" : "_pack_") + std::to_string(packId);
    if (!registration.spawnReason.empty())
        pack.actor.memorySummary = registration.spawnReason;
    else if (isDuty)
        pack.actor.memorySummary = "duty_mission_purge_mutant_infestation";
    else
        pack.actor.memorySummary = isSquad ? "session_squad_spawn" : "session_spawn";
    g_sessionPacks.push_back(pack);

    PackRegisterResult result;
    result.ok = true;
    result.packId = packId;
    return result;
}

void ResetSessionPacks()
{
    g_sessionPacks.clear();
    g_nextPackId = 1;
}

void ResetSessionPacksForTests() { ResetSessionPacks(); }

std::string FormatPackList()
{
    if (g_sessionPacks.empty())
        return "empty";

    std::ostringstream out;
    for (size_t i = 0; i < g_sessionPacks.size(); ++i)
    {
        const SessionPack& pack = g_sessionPacks[i];
        if (i != 0)
            out << " | ";
        out << "PACK#" << pack.packId << " " << pack.section << " members=" << pack.objectIds.size()
            << " ids=" << FormatObjectIds(pack.objectIds);
    }
    return out.str();
}

std::string ObservePack(uint32_t packId, const WorldState& world)
{
    const SessionPack* pack = FindPack(packId);
    if (!pack)
        return "unknown pack: " + std::to_string(packId);

    return BuildActorObservation(pack->actor, world, BuildPackSituation(*pack));
}

PackWakePrepareResult PreparePackWake(uint32_t packId, const WorldState& world, uint32_t gameDay)
{
    const SessionPack* pack = FindPack(packId);
    if (!pack)
    {
        PackWakePrepareResult result;
        result.reason = "unknown pack: " + std::to_string(packId);
        return result;
    }

    PackWakePrepareResult result;
    result.ok = true;
    result.context.actor = pack->actor;
    result.context.gameDay = gameDay;
    result.context.observation = BuildActorObservation(pack->actor, world, BuildPackSituation(*pack));
    result.context.prompt = BuildActorWakePrompt(pack->actor, world, BuildPackSituation(*pack));
    return result;
}

ActuatorResult ApplyPackWakeResult(uint32_t packId, WorldState& world, ActorRuntime& runtime,
    const ActorProviderResult& providerResult, uint32_t gameDay)
{
    SessionPack* pack = FindPack(packId);
    if (!pack)
    {
        ActuatorResult result;
        result.reason = "unknown pack: " + std::to_string(packId);
        return result;
    }
    return runtime.ApplyProviderResult(world, pack->actor, providerResult, gameDay);
}

ActuatorResult WakePack(uint32_t packId, WorldState& world, ActorRuntime& runtime, uint32_t gameDay)
{
    SessionPack* pack = FindPack(packId);
    if (!pack)
    {
        ActuatorResult result;
        result.reason = "unknown pack: " + std::to_string(packId);
        return result;
    }

    return runtime.Wake(world, pack->actor, BuildPackSituation(*pack), gameDay);
}
} // namespace xrSim
