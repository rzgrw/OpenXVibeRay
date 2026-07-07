#include "xrSim/xrSimActors.h"

#include <sstream>
#include <string>

namespace xrSim
{
namespace
{
std::string EscapeLineValue(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (const char ch : text)
    {
        if (ch == '\n')
        {
            out += "\\n";
        }
        else if (ch == '\r')
        {
            out += "\\r";
        }
        else
        {
            out += ch;
        }
    }
    return out;
}

std::string SummarizeIntent(const ActorIntentPlan& intent)
{
    if (intent.coast)
        return "coast";

    std::ostringstream out;
    bool first = true;
    const auto appendField = [&out, &first](const std::string& key, const std::string& value)
    {
        if (value.empty())
            return;
        if (!first)
            out << " ";
        out << key << "=" << value;
        first = false;
    };

    appendField("goal", intent.goal);
    appendField("stance", intent.stance);
    if (intent.durationMs != 0)
    {
        if (!first)
            out << " ";
        out << "duration_ms=" << intent.durationMs;
        first = false;
    }
    if (!intent.actions.empty())
    {
        if (!first)
            out << " ";
        out << "actions=" << intent.actions.size();
        first = false;
    }
    if (!intent.memories.empty())
    {
        if (!first)
            out << " ";
        out << "memories=" << intent.memories.size();
    }
    return out.str();
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

const char* ActorLegalTools(ActorAgentScope scope)
{
    switch (scope)
    {
    case ActorAgentScope::Squad: return "move_to fire_pattern vocalize retreat author_memory adjust_population";
    case ActorAgentScope::MutantPack: return "stalk ambush retreat author_memory adjust_population";
    case ActorAgentScope::Zone: return "author_memory adjust_population";
    }
    return "author_memory adjust_population";
}

bool IsActorActionLegal(ActorAgentScope scope, const std::string& verb)
{
    if (verb == "author_memory" || verb == "adjust_population")
        return true;
    if (scope == ActorAgentScope::Squad)
        return verb == "move_to" || verb == "fire_pattern" || verb == "vocalize" || verb == "retreat";
    if (scope == ActorAgentScope::MutantPack)
        return verb == "stalk" || verb == "ambush" || verb == "retreat";
    return false;
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
    out << "name " << EscapeLineValue(actor.name) << "\n";
    out << "memory_summary " << EscapeLineValue(actor.memorySummary) << "\n";
    if (!actor.lastIntent.goal.empty())
        out << "current_goal " << EscapeLineValue(actor.lastIntent.goal) << "\n";
    const std::string lastIntentSummary = SummarizeIntent(actor.lastIntent);
    if (!lastIntentSummary.empty())
        out << "last_intent " << EscapeLineValue(lastIntentSummary) << "\n";
    out << "situation " << EscapeLineValue(situation) << "\n";
    out << "world_digest " << EscapeLineValue(world.Digest()) << "\n";
    out << "legal_tools " << ActorLegalTools(actor.scope) << "\n";
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
