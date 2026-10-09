#pragma once

#include "xrSim/xrSimDecisionLedger.h"

namespace xrSim
{
struct AgentBelief
{
    std::string key;
    std::string value;
    uint8_t confidence = 0;
};

struct AgentEpisode
{
    uint64_t eventSeq = 0;
    uint64_t tick = 0;
    std::string kind;
    std::string summary;
};

struct WorldAgentRecord
{
    uint32_t agentId = 0;
    ScopeId scope;
    std::string name;
    uint32_t charterVersion = 1;
    std::string charter;
    std::string memorySummary;
    std::vector<AgentBelief> beliefs;
    std::vector<AgentEpisode> episodes;
    uint64_t lastPlanId = 0;
    uint64_t lastWakeMs = 0;
};

struct WorldWakeContext
{
    uint64_t requestId = 0;
    WorldAgentRecord agent;
    uint64_t baseRevision = 0;
    uint64_t tick = 0;
    std::string wakeReason;
    std::string observation;
    std::string prompt;
};

std::string BuildScopedProjection(const ZoneState& world, const WorldAgentRecord& agent);
WorldWakeContext PrepareWorldWake(uint64_t requestId, const ZoneState& world,
    const WorldAgentRecord& agent, uint64_t tick, const std::string& wakeReason);
void ApplyObservedConsequence(WorldAgentRecord& agent, const WorldEvent& event);
std::vector<WorldAgentRecord> CreateWorldAgents(const ZoneManifest& manifest);
} // namespace xrSim
