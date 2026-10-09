#include "xrSim/xrSimIntentCommit.h"

#include <algorithm>

namespace xrSim
{
namespace
{
bool IsOpPermittedForScope(AgentScopeKind scopeKind, PlanOpKind opKind)
{
    switch (scopeKind)
    {
    case AgentScopeKind::Zone:
        return opKind == PlanOpKind::SetObjective;
    case AgentScopeKind::Faction:
        return opKind == PlanOpKind::SetObjective || opKind == PlanOpKind::Claim || opKind == PlanOpKind::Release ||
            opKind == PlanOpKind::Relate;
    case AgentScopeKind::Ecology:
        return opKind == PlanOpKind::AdjustPopulation || opKind == PlanOpKind::SetMigration;
    case AgentScopeKind::Economy:
        return opKind == PlanOpKind::SetSupply || opKind == PlanOpKind::SetDemand || opKind == PlanOpKind::SetRoute;
    case AgentScopeKind::Incident:
        return opKind == PlanOpKind::ProposeIncident;
    case AgentScopeKind::Region:
        return opKind == PlanOpKind::Create || opKind == PlanOpKind::Dissolve || opKind == PlanOpKind::Dispatch;
    case AgentScopeKind::Squad:
    case AgentScopeKind::MutantPack:
        return opKind == PlanOpKind::Move || opKind == PlanOpKind::Retreat || opKind == PlanOpKind::Resupply ||
            opKind == PlanOpKind::SetObjective || opKind == PlanOpKind::ClearObjective;
    case AgentScopeKind::NamedActor:
        return opKind == PlanOpKind::SetObjective || opKind == PlanOpKind::ClearObjective;
    }
    return false;
}

ObjectiveKind ParseObjectiveKind(const std::string& verb)
{
    if (verb == "move")
        return ObjectiveKind::Move;
    if (verb == "retreat")
        return ObjectiveKind::Retreat;
    if (verb == "resupply")
        return ObjectiveKind::Resupply;
    if (verb == "hunt")
        return ObjectiveKind::Hunt;
    if (verb == "avoid")
        return ObjectiveKind::Avoid;
    if (verb == "reinforce")
        return ObjectiveKind::Reinforce;
    return ObjectiveKind::Hold;
}
} // namespace

StageIntentResult StageIntent(
    const ZoneState& world, AuthorityRegistry& authority, const IntentEnvelope& envelope, uint64_t currentTick)
{
    if (envelope.baseRevision != world.Revision())
        return StageIntentResult{ false, "stale_revision", {} };

    const AuthorityResult can = authority.CanAuthor(envelope.scope, ObjectiveAuthor::Ai, envelope.agentId);
    if (!can.ok)
        return StageIntentResult{ false, can.reason, {} };

    StagedIntent staged;
    staged.envelope = envelope;
    staged.state = world;

    for (const auto& op : envelope.operations)
    {
        if (!IsOpPermittedForScope(envelope.scope.kind, op.kind))
            return StageIntentResult{ false, "operation_not_permitted_for_scope", {} };

        if (op.kind == PlanOpKind::SetObjective || op.kind == PlanOpKind::Move || op.kind == PlanOpKind::Retreat ||
            op.kind == PlanOpKind::Resupply || op.kind == PlanOpKind::Dispatch)
        {
            Handle targetHandle = staged.state.FindLocationByName(op.target);
            if (!targetHandle.IsValid())
                targetHandle = staged.state.FindRouteByName(op.target);
            if (!targetHandle.IsValid())
                return StageIntentResult{ false, "unknown_target: " + op.target, {} };

            if (op.expiresAtTick <= currentTick || op.expiresAtTick > currentTick + 36000)
                return StageIntentResult{ false, "invalid_objective_expiry", {} };

            Handle subjectHandle = Handle::Invalid();
            ObjectiveSubjectKind subjectKind = ObjectiveSubjectKind::Group;

            if (envelope.scope.kind == AgentScopeKind::Squad || envelope.scope.kind == AgentScopeKind::MutantPack)
            {
                const GroupRecord* grp = staged.state.FindGroup(staged.state.FindGroupByName(op.subject));
                if (!grp || grp->agentId != envelope.agentId)
                    return StageIntentResult{ false, "subject_must_match_envelope_group", {} };
                subjectHandle = grp->id;
                subjectKind = ObjectiveSubjectKind::Group;
            }
            else if (envelope.scope.kind == AgentScopeKind::Faction)
            {
                const FactionRecord* f = staged.state.FindFaction(staged.state.FindFactionByName(op.subject));
                if (!f || f->agentId != envelope.agentId)
                    return StageIntentResult{ false, "subject_must_match_envelope_faction", {} };
                subjectHandle = f->id;
                subjectKind = ObjectiveSubjectKind::Faction;
            }
            else
            {
                subjectKind = ObjectiveSubjectKind::Scope;
            }

            for (auto& obj : staged.state.MutableObjectives())
            {
                if (obj.subjectName == op.subject && obj.active)
                    obj.active = false;
            }

            ObjectiveRecord record;
            record.planId = envelope.requestId;
            record.scopeKind = uint32_t(envelope.scope.kind);
            record.scopeId = envelope.scope.id;
            record.subjectKind = subjectKind;
            record.subject = subjectHandle;
            record.subjectName = op.subject;
            record.kind = ParseObjectiveKind(op.verb);
            record.target = targetHandle;
            record.expiresAtTick = op.expiresAtTick;
            record.author = ObjectiveAuthor::Ai;
            record.active = true;
            staged.state.MutableObjectives().push_back(record);

            AppliedDelta delta;
            delta.kind = op.kind;
            delta.subject = op.subject;
            delta.before = "none";
            delta.after = op.target;
            staged.deltas.push_back(delta);
        }
        else if (op.kind == PlanOpKind::SetSupply || op.kind == PlanOpKind::SetDemand)
        {
            ResourceRecord* r = staged.state.FindResource(staged.state.FindResourceByName(op.subject));
            if (!r)
                return StageIntentResult{ false, "unknown_resource: " + op.subject, {} };
            if (op.amount < -20 || op.amount > 20)
                return StageIntentResult{ false, "invalid_resource_amount", {} };

            const int32_t beforeVal = (op.kind == PlanOpKind::SetSupply ? r->quantity : r->demand);
            const int32_t nextVal = beforeVal + op.amount;
            if (nextVal < r->minimum || nextVal > r->maximum)
                return StageIntentResult{ false, "resource_bounds_exceeded", {} };

            if (op.kind == PlanOpKind::SetSupply)
                r->quantity = nextVal;
            else
                r->demand = nextVal;

            AppliedDelta delta;
            delta.kind = op.kind;
            delta.subject = op.subject;
            delta.before = std::to_string(beforeVal);
            delta.after = std::to_string(nextVal);
            staged.deltas.push_back(delta);
        }
        else if (op.kind == PlanOpKind::Relate)
        {
            const Handle f1 = staged.state.FindFactionByName(op.subject);
            const Handle f2 = staged.state.FindFactionByName(op.target);
            if (!f1.IsValid() || !f2.IsValid() || f1 == f2)
                return StageIntentResult{ false, "invalid_relate_factions", {} };
            if (op.amount < -10 || op.amount > 10)
                return StageIntentResult{ false, "invalid_relate_amount", {} };

            staged.state.SetRelation(f1, f2, int16_t(std::clamp(op.amount, -100, 100)));
            AppliedDelta delta;
            delta.kind = op.kind;
            delta.subject = op.subject + "<->" + op.target;
            delta.before = "0";
            delta.after = std::to_string(op.amount);
            staged.deltas.push_back(delta);
        }
        else if (op.kind == PlanOpKind::AdjustPopulation)
        {
            GroupRecord* grp = staged.state.FindGroup(staged.state.FindGroupByName(op.subject));
            if (!grp)
                return StageIntentResult{ false, "unknown_group: " + op.subject, {} };
            if (op.amount < -8 || op.amount > 8)
                return StageIntentResult{ false, "invalid_population_delta", {} };

            const int32_t nextCount = int32_t(grp->aliveCount) + op.amount;
            if (nextCount < 0 || uint32_t(nextCount) > grp->initialCount)
                return StageIntentResult{ false, "population_bounds_exceeded", {} };

            const uint32_t beforeCount = grp->aliveCount;
            grp->aliveCount = uint32_t(nextCount);

            AppliedDelta delta;
            delta.kind = op.kind;
            delta.subject = op.subject;
            delta.before = std::to_string(beforeCount);
            delta.after = std::to_string(grp->aliveCount);
            staged.deltas.push_back(delta);
        }
        else if (op.kind == PlanOpKind::SetRoute)
        {
            RouteRecord* route = staged.state.FindRoute(staged.state.FindRouteByName(op.subject));
            if (!route)
                return StageIntentResult{ false, "unknown_route: " + op.subject, {} };
            const bool open = op.verb == "open";
            if (op.verb != "open" && op.verb != "closed")
                return StageIntentResult{ false, "invalid_route_verb: " + op.verb, {} };

            route->open = open;
            AppliedDelta delta;
            delta.kind = op.kind;
            delta.subject = op.subject;
            delta.before = route->open ? "open" : "closed";
            delta.after = open ? "open" : "closed";
            staged.deltas.push_back(delta);
        }
        else if (op.kind == PlanOpKind::ProposeIncident)
        {
            if (op.amount < 0 || op.amount > 10)
                return StageIntentResult{ false, "invalid_incident_magnitude", {} };

            IncidentRecord incident;
            incident.id = envelope.requestId;
            incident.location = staged.state.FindLocationByName(op.target);
            incident.kind = op.verb;
            incident.magnitude = op.amount;
            incident.pending = true;
            staged.state.AppendIncident(incident);

            AppliedDelta delta;
            delta.kind = op.kind;
            delta.subject = op.subject;
            delta.before = "none";
            delta.after = "pending";
            staged.deltas.push_back(delta);
        }
        else if (op.kind == PlanOpKind::ClearObjective)
        {
            bool found = false;
            for (auto& obj : staged.state.MutableObjectives())
            {
                if (obj.subjectName == op.subject && obj.active && obj.scopeKind == uint32_t(envelope.scope.kind) &&
                    obj.scopeId == envelope.scope.id)
                {
                    obj.active = false;
                    found = true;
                }
            }
            if (!found)
                return StageIntentResult{ false, "objective_not_found_to_clear", {} };

            AppliedDelta delta;
            delta.kind = op.kind;
            delta.subject = op.subject;
            delta.before = "active";
            delta.after = "cleared";
            staged.deltas.push_back(delta);
        }
        else
        {
            return StageIntentResult{ false, "unimplemented_operation", {} };
        }
    }

    return StageIntentResult{ true, "", std::move(staged) };
}

CommitIntentResult CommitStagedIntent(
    ZoneState& world, AuthorityRegistry& authority, StagedIntent&& staged, uint64_t currentTick)
{
    if (world.Revision() != staged.envelope.baseRevision)
        return CommitIntentResult{ false, true, "stale_revision", world.Revision(), 0, {}, {} };

    const AuthorityResult can =
        authority.CanAuthor(staged.envelope.scope, ObjectiveAuthor::Ai, staged.envelope.agentId);
    if (!can.ok)
        return CommitIntentResult{ false, false, can.reason, world.Revision(), 0, {}, {} };

    const uint64_t newRevision = staged.envelope.baseRevision + 1;
    staged.state.SetRevision(newRevision);

    const uint64_t expiresAt = currentTick + staged.envelope.horizonTicks;
    authority.RecordPlan(
        staged.envelope.scope, staged.envelope.agentId, staged.envelope.requestId, expiresAt, newRevision);

    CommitIntentResult result;
    result.ok = true;
    result.stale = false;
    result.committedRevision = newRevision;
    result.planId = staged.envelope.requestId;
    result.deltas = std::move(staged.deltas);
    result.events = std::move(staged.events);

    world = std::move(staged.state);
    return result;
}
} // namespace xrSim
