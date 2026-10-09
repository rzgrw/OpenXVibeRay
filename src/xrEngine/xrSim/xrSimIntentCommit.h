#pragma once

#include "xrSim/xrSimIntentEnvelope.h"

namespace xrSim
{
struct AppliedDelta
{
    PlanOpKind kind = PlanOpKind::SetObjective;
    std::string subject;
    std::string before;
    std::string after;
};

struct StagedIntent
{
    IntentEnvelope envelope;
    ZoneState state;
    std::vector<AppliedDelta> deltas;
    std::vector<WorldEvent> events;
};

struct StageIntentResult
{
    bool ok = false;
    std::string reason;
    StagedIntent staged;
};

struct CommitIntentResult
{
    bool ok = false;
    bool stale = false;
    std::string reason;
    uint64_t committedRevision = 0;
    uint64_t planId = 0;
    std::vector<AppliedDelta> deltas;
    std::vector<WorldEvent> events;
};

StageIntentResult StageIntent(const ZoneState& world, AuthorityRegistry& authority,
    const IntentEnvelope& envelope, uint64_t currentTick);

CommitIntentResult CommitStagedIntent(ZoneState& world, AuthorityRegistry& authority,
    StagedIntent&& staged, uint64_t currentTick);
} // namespace xrSim
