#include "xrSim/xrSimMaterialization.h"

#include <set>

namespace xrSim
{
namespace
{
constexpr uint32_t IndexMask = 0x00ffffffu;
constexpr uint32_t GenerationShift = 24;

Handle MakeHandle(uint32_t index, uint8_t generation = 1)
{
    return Handle{ (uint32_t(generation) << GenerationShift) | (index & IndexMask) };
}
} // namespace

Materializer::Materializer(IAgentZoneLiveAdapter& adapter) : m_adapter(adapter) {}

const LiveBinding* Materializer::FindBinding(Handle group) const
{
    for (const auto& b : m_bindings)
    {
        if (b.group == group)
            return &b;
    }
    return nullptr;
}

const std::vector<LiveBinding>& Materializer::Bindings() const
{
    return m_bindings;
}

FixedPosition Materializer::ToLivePosition(const FixedPosition& relative) const
{
    FixedPosition live = relative;
    live.xCm += m_origin.xCm;
    live.yCm += m_origin.yCm;
    live.zCm += m_origin.zCm;
    return live;
}

void Materializer::ReleaseAll()
{
    for (const auto& b : m_bindings)
    {
        m_adapter.ReleaseObjective(b);
        m_adapter.DestroyObjects(b.objectIds);
    }
    m_bindings.clear();
}

MaterializeResult Materializer::Bootstrap(const ZoneState& world, uint32_t spawnBudget)
{
    if (!m_originCaptured)
    {
        const ReadPositionResult originResult = m_adapter.CapturePlayerOrigin();
        if (!originResult.ok)
            return MaterializeResult{ false, false, "origin_capture_failed: " + originResult.reason, 0 };
        m_origin = originResult.position;
        m_originCaptured = true;
    }

    std::set<uint16_t> usedIds;
    for (const auto& b : m_bindings)
    {
        for (const uint16_t id : b.objectIds)
            usedIds.insert(id);
    }

    uint32_t applied = 0;
    uint32_t totalGroups = 0;
    uint32_t boundGroups = 0;

    for (uint32_t i = 0;; ++i)
    {
        const Handle handle = MakeHandle(i, 1);
        const GroupRecord* grp = world.FindGroup(handle);
        if (!grp)
            break;

        ++totalGroups;
        if (FindBinding(handle))
        {
            ++boundGroups;
            continue;
        }

        if (applied >= spawnBudget)
            continue;

        const LocationRecord* loc = world.FindLocation(grp->location);
        FixedPosition spawnPos = m_origin;
        if (loc)
            spawnPos = ToLivePosition(loc->position);

        const LiveAdapterResult val = m_adapter.ValidateSection(grp->section);
        if (!val.ok)
            return MaterializeResult{ false, false, "invalid_section: " + grp->section, applied };

        LiveSpawnRequest req;
        req.group = grp->id;
        req.section = grp->section;
        req.count = grp->initialCount;
        req.position = spawnPos;

        const SpawnGroupResult spawned = m_adapter.SpawnGroup(req);
        if (!spawned.ok || spawned.objectIds.size() != req.count)
        {
            m_adapter.DestroyObjects(spawned.objectIds);
            return MaterializeResult{ false, false, "spawn_group_failed: " + spawned.reason, applied };
        }

        for (const uint16_t id : spawned.objectIds)
        {
            if (!usedIds.insert(id).second)
            {
                m_adapter.DestroyObjects(spawned.objectIds);
                return MaterializeResult{ false, false, "duplicate_object_id_spawned", applied };
            }
        }

        LiveBinding binding;
        binding.group = grp->id;
        binding.objectIds = spawned.objectIds;
        binding.boundRevision = world.Revision();
        binding.activePlanId = 0;
        binding.lastObservationTick = 0;
        binding.tacticalCombat = false;
        binding.calmSinceTick = 0;

        m_bindings.push_back(std::move(binding));
        ++applied;
        ++boundGroups;
    }

    const bool complete = (boundGroups == totalGroups);
    return MaterializeResult{ true, complete, "", applied };
}

MaterializeResult Materializer::ApplyCommittedState(
    const ZoneState& world, uint32_t commandBudget, uint64_t currentTick)
{
    (void)currentTick;
    uint32_t applied = 0;

    for (const auto& obj : world.Objectives())
    {
        if (!obj.active || obj.subjectKind != ObjectiveSubjectKind::Group)
            continue;

        LiveBinding* targetBinding = nullptr;
        for (auto& b : m_bindings)
        {
            if (b.group == obj.subject)
            {
                targetBinding = &b;
                break;
            }
        }

        if (!targetBinding || targetBinding->activePlanId == obj.planId)
            continue;

        if (applied >= commandBudget)
            break;

        FixedPosition targetPos = m_origin;
        const LocationRecord* loc = world.FindLocation(obj.target);
        if (loc)
            targetPos = ToLivePosition(loc->position);

        LiveObjectiveCommand cmd;
        cmd.group = targetBinding->group;
        cmd.planId = obj.planId;
        cmd.kind = obj.kind;
        cmd.target = targetPos;
        cmd.expiresAtTick = obj.expiresAtTick;

        const LiveAdapterResult res = m_adapter.ApplyObjective(*targetBinding, cmd);
        if (res.ok)
        {
            targetBinding->activePlanId = obj.planId;
            ++applied;
        }
    }

    return MaterializeResult{ true, true, "", applied };
}

ObservationBatch Materializer::PollObservations(const ZoneState& world, uint64_t currentTick)
{
    ObservationBatch batch;
    batch.ok = true;
    uint64_t eventSeq = 1;

    for (auto& b : m_bindings)
    {
        const ObserveGroupResult obs = m_adapter.ObserveGroup(b);
        if (!obs.ok)
            continue;

        const GroupRecord* grp = world.FindGroup(b.group);
        if (!grp)
            continue;

        WorldEvent event;
        event.seq = eventSeq++;
        event.tick = currentTick;
        event.kind = "group_alive_count_observed";
        event.subject = b.group;
        event.amount = int32_t(obs.aliveCount);
        event.source = "live_observation";
        batch.events.push_back(event);

        if (obs.inCombat != b.tacticalCombat)
        {
            b.tacticalCombat = obs.inCombat;
            WorldEvent combatEvent;
            combatEvent.seq = eventSeq++;
            combatEvent.tick = currentTick;
            combatEvent.kind = obs.inCombat ? "combat_entered" : "combat_left";
            combatEvent.subject = b.group;
            combatEvent.amount = obs.inCombat ? 1 : 0;
            combatEvent.source = "live_observation";
            batch.events.push_back(combatEvent);
        }
    }

    return batch;
}
} // namespace xrSim
