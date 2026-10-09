#pragma once

#include "xrSim/xrSimAuthority.h"

#include <cstdint>
#include <string>
#include <vector>

namespace xrSim
{
enum class PlanOpKind : uint8_t
{
    Create,
    Dissolve,
    Dispatch,
    Move,
    Retreat,
    Resupply,
    Claim,
    Release,
    Relate,
    SetObjective,
    ClearObjective,
    AdjustPopulation,
    SetMigration,
    SetSupply,
    SetDemand,
    SetRoute,
    ProposeIncident
};

struct PlanOp
{
    PlanOpKind kind = PlanOpKind::SetObjective;
    std::string subject;
    std::string verb;
    std::string target;
    int32_t amount = 0;
    uint64_t expiresAtTick = 0;
};

struct IntentEnvelope
{
    uint64_t requestId = 0;
    uint32_t agentId = 0;
    ScopeId scope;
    uint64_t baseRevision = 0;
    uint32_t horizonTicks = 0;
    uint8_t priority = 0;
    std::string wakeReason;
    std::string rationaleSummary;
    std::vector<std::string> preconditions;
    std::vector<std::string> constraints;
    std::vector<PlanOp> operations;
};

struct IntentParseResult
{
    bool ok = false;
    std::string reason;
    IntentEnvelope envelope;
};

IntentParseResult ParseWorldIntentEnvelope(const std::string& text);
std::string FormatWorldIntentEnvelope(const IntentEnvelope& envelope);
} // namespace xrSim
