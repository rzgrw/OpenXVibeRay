#include "xrSim/xrSimActors.h"

#include <sstream>

namespace xrSim
{
namespace
{
const char* LegalToolsForScope(ActorAgentScope scope)
{
    switch (scope)
    {
    case ActorAgentScope::Squad: return "move_to fire_pattern vocalize retreat author_memory";
    case ActorAgentScope::MutantPack: return "stalk ambush retreat author_memory";
    case ActorAgentScope::Zone: return "author_memory";
    }
    return "author_memory";
}
} // namespace

const char* ActorScopeName(ActorAgentScope scope)
{
    switch (scope)
    {
    case ActorAgentScope::Zone: return "ZONE";
    case ActorAgentScope::Squad: return "SQUAD";
    case ActorAgentScope::MutantPack: return "MUTANT_PACK";
    }
    return "UNKNOWN";
}

ActorAgentRecord MakeDebugSquadAgent()
{
    ActorAgentRecord actor;
    actor.agentId = 101;
    actor.scope = ActorAgentScope::Squad;
    actor.name = "debug_stalker_squad";
    actor.memorySummary = "met_player_recently cautious_about_grenades";
    return actor;
}

ActorAgentRecord MakeDebugMutantPackAgent()
{
    ActorAgentRecord actor;
    actor.agentId = 201;
    actor.scope = ActorAgentScope::MutantPack;
    actor.name = "debug_blind_dog_pack";
    actor.memorySummary = "hungry_guarding_garbage_den";
    return actor;
}

std::string BuildActorObservation(
    const ActorAgentRecord& actor, const WorldState& world, const std::string& situation)
{
    std::ostringstream out;
    out << "agent_id " << actor.agentId << "\n";
    out << "scope " << ActorScopeName(actor.scope) << "\n";
    out << "name " << actor.name << "\n";
    out << "memory_summary " << actor.memorySummary << "\n";
    out << "situation " << situation << "\n";
    out << "world_digest " << world.Digest() << "\n";
    out << "legal_tools " << LegalToolsForScope(actor.scope) << "\n";
    return out.str();
}

std::string BuildActorWakePrompt(
    const ActorAgentRecord& actor, const WorldState& world, const std::string& situation)
{
    std::ostringstream out;
    out << "xrsim_actor_wake_v1\n";
    out << BuildActorObservation(actor, world, situation);
    out << "rules\n";
    out << "return xrsim_actor_intent_v1\n";
    out << "llm_owns_decisions cxx_owns_embodiment\n";
    out << "do_not_emit_unlisted_tools\n";
    out << "end\n";
    return out.str();
}
} // namespace xrSim
