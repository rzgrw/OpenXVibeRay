#include "xrSim/xrSimWorldAgents.h"

#include <sstream>

namespace xrSim
{
namespace
{
AgentScopeKind ParseScopeKindString(const std::string& text)
{
    if (text == "zone") return AgentScopeKind::Zone;
    if (text == "faction") return AgentScopeKind::Faction;
    if (text == "ecology") return AgentScopeKind::Ecology;
    if (text == "economy") return AgentScopeKind::Economy;
    if (text == "incident") return AgentScopeKind::Incident;
    if (text == "region") return AgentScopeKind::Region;
    if (text == "squad") return AgentScopeKind::Squad;
    if (text == "mutant_pack") return AgentScopeKind::MutantPack;
    if (text == "named_actor") return AgentScopeKind::NamedActor;
    return AgentScopeKind::Zone;
}

std::string GetCharterForCharKey(const std::string& key)
{
    if (key == "zone_director_v1")
        return "Zone director arbitrates high-level zone dynamics and cross-owner conflict. Model narration is not a fact; all effects must use typed operations.";
    if (key == "stalker_faction_v1")
        return "Stalker faction sets stalker objectives, claims locations, and manages diplomacy. Model narration is not a fact; all effects must use typed operations.";
    if (key == "bandit_faction_v1")
        return "Bandit faction sets bandit objectives, claims territory, and manages raids. Model narration is not a fact; all effects must use typed operations.";
    if (key == "ecology_v1")
        return "Zone ecology regulates mutant pack populations and migration corridors. Model narration is not a fact; all effects must use typed operations.";
    if (key == "economy_v1")
        return "Zone economy manages resource caches, supplies, and route flow. Model narration is not a fact; all effects must use typed operations.";
    if (key == "region_v1")
        return "Region authority manages regional spawns and dispatches. Model narration is not a fact; all effects must use typed operations.";
    if (key == "squad_v1")
        return "Squad leader commands movement, stance, and tactical objectives. Model narration is not a fact; all effects must use typed operations.";
    if (key == "pack_v1")
        return "Mutant pack alpha controls pack hunting, territory, and retreat. Model narration is not a fact; all effects must use typed operations.";
    return "Standard world agent charter. Model narration is not a fact; all effects must use typed operations.";
}
} // namespace

std::string BuildScopedProjection(const ZoneState& world, const WorldAgentRecord& agent)
{
    std::ostringstream out;
    out << "scoped_projection_v1\n";
    out << "agent_id " << agent.agentId << "\n";
    out << "scope " << uint32_t(agent.scope.kind) << ":" << agent.scope.id << "\n";
    out << "revision " << world.Revision() << "\n";

    out << "charter_begin\n" << agent.charter << "\ncharter_end\n";

    if (!agent.memorySummary.empty())
        out << "memory_summary " << agent.memorySummary << "\n";

    out << "private_beliefs " << agent.beliefs.size() << "\n";
    for (const auto& b : agent.beliefs)
        out << "belief " << b.key << " " << b.value << " " << uint32_t(b.confidence) << "\n";

    out << "private_episodes " << agent.episodes.size() << "\n";
    for (const auto& ep : agent.episodes)
        out << "episode seq=" << ep.eventSeq << " tick=" << ep.tick << " kind=" << ep.kind
            << " summary=" << ep.summary << "\n";

    out << "canonical_facts " << world.Digest() << "\n";

    return out.str();
}

WorldWakeContext PrepareWorldWake(
    uint64_t requestId,
    const ZoneState& world,
    const WorldAgentRecord& agent,
    uint64_t tick,
    const std::string& wakeReason)
{
    WorldWakeContext context;
    context.requestId = requestId;
    context.agent = agent;
    context.baseRevision = world.Revision();
    context.tick = tick;
    context.wakeReason = wakeReason;
    context.observation = BuildScopedProjection(world, agent);

    std::ostringstream prompt;
    prompt << "xrsim_world_wake_v1\n";
    prompt << "request_id " << requestId << "\n";
    prompt << "agent_id " << agent.agentId << "\n";
    prompt << "scope " << uint32_t(agent.scope.kind) << " " << agent.scope.id << "\n";
    prompt << "base_revision " << world.Revision() << "\n";
    prompt << "tick " << tick << "\n";
    prompt << "wake_reason " << wakeReason << "\n";
    prompt << "observation_begin\n" << context.observation << "observation_end\n";
    prompt << "rules\n";
    prompt << "return xrsim_world_intent_v1 with valid operations, or coast\n";
    prompt << "model narration is not a fact; all mutations must be typed operations\n";
    prompt << "end\n";
    context.prompt = prompt.str();

    return context;
}

void ApplyObservedConsequence(WorldAgentRecord& agent, const WorldEvent& event)
{
    AgentEpisode episode;
    episode.eventSeq = event.seq;
    episode.tick = event.tick;
    episode.kind = event.kind;
    episode.summary = "kind=" + event.kind + " src=" + event.source + " amt=" + std::to_string(event.amount);
    agent.episodes.push_back(episode);

    if (agent.episodes.size() > 32)
        agent.episodes.erase(agent.episodes.begin(), agent.episodes.begin() + (agent.episodes.size() - 32));

    if (agent.memorySummary.empty())
        agent.memorySummary = event.kind;
    else
        agent.memorySummary = agent.memorySummary + " | " + event.kind;

    if (agent.memorySummary.size() > 2048)
        agent.memorySummary.resize(2048);
}

std::vector<WorldAgentRecord> CreateWorldAgents(const ZoneManifest& manifest)
{
    std::vector<WorldAgentRecord> agents;
    for (const auto& a : manifest.agents)
    {
        WorldAgentRecord agent;
        agent.agentId = a.agentId;
        agent.scope = { ParseScopeKindString(a.scopeKind), a.scopeId };
        agent.name = a.name;
        agent.charterVersion = 1;
        agent.charter = GetCharterForCharKey(a.charterKey);
        agents.push_back(agent);
    }
    return agents;
}
} // namespace xrSim
