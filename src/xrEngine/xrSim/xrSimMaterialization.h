#pragma once

#include "xrSim/xrSimIntentCommit.h"

#include <cstdint>
#include <string>
#include <vector>

namespace xrSim
{
struct LiveAdapterResult
{
    bool ok = false;
    std::string reason;
};

struct ReadPositionResult
{
    bool ok = false;
    std::string reason;
    FixedPosition position;
};

struct LiveSpawnRequest
{
    Handle group;
    std::string section;
    uint32_t count = 0;
    FixedPosition position;
};

struct SpawnGroupResult
{
    bool ok = false;
    std::string reason;
    std::vector<uint16_t> objectIds;
};

struct LiveBinding
{
    Handle group;
    std::vector<uint16_t> objectIds;
    uint64_t boundRevision = 0;
    uint64_t activePlanId = 0;
    uint64_t lastObservationTick = 0;
    uint32_t lastAliveCount = 0xffffffffu;
    bool tacticalCombat = false;
    uint64_t calmSinceTick = 0;
};

struct LiveObjectiveCommand
{
    Handle group;
    uint64_t planId = 0;
    ObjectiveKind kind = ObjectiveKind::Hold;
    FixedPosition target;
    uint64_t expiresAtTick = 0;
};

struct ObserveGroupResult
{
    bool ok = false;
    std::string reason;
    uint32_t aliveCount = 0;
    bool inCombat = false;
    FixedPosition centroid;
    uint32_t healthPermille = 0;
};

class IAgentZoneLiveAdapter
{
public:
    virtual ~IAgentZoneLiveAdapter() = default;

    virtual ReadPositionResult CapturePlayerOrigin()
    {
        return { false, "live_adapter_unavailable", {} };
    }

    virtual LiveAdapterResult ValidateSection(const std::string&)
    {
        return { false, "live_adapter_unavailable" };
    }

    virtual SpawnGroupResult SpawnGroup(const LiveSpawnRequest&)
    {
        return { false, "live_adapter_unavailable", {} };
    }

    virtual LiveAdapterResult DestroyObjects(const std::vector<uint16_t>&)
    {
        return { false, "live_adapter_unavailable" };
    }

    virtual LiveAdapterResult ApplyObjective(
        const LiveBinding& binding, const LiveObjectiveCommand& command)
    {
        (void)binding;
        (void)command;
        return { false, "live_adapter_unavailable" };
    }

    virtual ObserveGroupResult ObserveGroup(const LiveBinding&)
    {
        return { false, "live_adapter_unavailable", 0, false, {}, 0 };
    }

    virtual LiveAdapterResult ReleaseObjective(const LiveBinding&)
    {
        return { false, "live_adapter_unavailable" };
    }

    virtual LiveAdapterResult SetPlayerAt(const FixedPosition&)
    {
        return { false, "live_adapter_unavailable" };
    }

    virtual LiveAdapterResult FacePlayerAt(const LiveBinding& binding)
    {
        (void)binding;
        return { false, "live_adapter_unavailable" };
    }

    virtual LiveAdapterResult DamageGroupFromPlayer(const LiveBinding& binding, uint32_t damagePermille)
    {
        (void)binding;
        (void)damagePermille;
        return { false, "live_adapter_unavailable" };
    }
};

struct MaterializeResult
{
    bool ok = false;
    bool complete = false;
    std::string reason;
    uint32_t applied = 0;
};

struct ObservationBatch
{
    bool ok = false;
    std::string reason;
    std::vector<WorldEvent> events;
};

class Materializer
{
public:
    explicit Materializer(IAgentZoneLiveAdapter& adapter);

    MaterializeResult Bootstrap(const ZoneState& world, uint32_t spawnBudget);
    // Combat handoff: a group that entered tactical combat has its objective released
    // (legacy AI is authoritative while an enemy exists) and the committed plan is
    // re-applied only after it has been calm for calmReacquireTicks ticks.
    MaterializeResult ApplyCommittedState(const ZoneState& world, uint32_t commandBudget, uint64_t currentTick,
        uint64_t calmReacquireTicks = 20);
    ObservationBatch PollObservations(const ZoneState& world, uint64_t currentTick);

    const LiveBinding* FindBinding(Handle group) const;
    const std::vector<LiveBinding>& Bindings() const;
    FixedPosition ToLivePosition(const FixedPosition& relative) const;
    void ReleaseAll();

private:
    IAgentZoneLiveAdapter& m_adapter;
    FixedPosition m_origin;
    bool m_originCaptured = false;
    std::vector<LiveBinding> m_bindings;
};
} // namespace xrSim
