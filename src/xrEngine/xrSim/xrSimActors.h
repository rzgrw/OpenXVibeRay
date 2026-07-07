#pragma once

#include "xrSim/xrSimActorIntent.h"
#include "xrSim/xrSimWorldState.h"

#include <cstdint>
#include <string>

namespace xrSim
{
enum class ActorAgentScope
{
    Zone,
    Squad,
    MutantPack,
};

struct ActorAgentRecord
{
    uint32_t agentId = 0;
    ActorAgentScope scope = ActorAgentScope::Zone;
    std::string name;
    std::string memorySummary;
    ActorIntentPlan lastIntent;
};

const char* ActorScopeName(ActorAgentScope scope);
ActorAgentRecord MakeDebugSquadAgent();
ActorAgentRecord MakeDebugMutantPackAgent();
std::string BuildActorObservation(
    const ActorAgentRecord& actor, const WorldState& world, const std::string& situation);
std::string BuildActorWakePrompt(
    const ActorAgentRecord& actor, const WorldState& world, const std::string& situation);
} // namespace xrSim
