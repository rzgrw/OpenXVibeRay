#include "xrSim/xrSimIntentEnvelope.h"

#include <cerrno>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace xrSim
{
namespace
{
constexpr size_t MaxEnvelopeBytes = 32 * 1024;
constexpr size_t MaxStringLength = 512;
constexpr size_t MaxOperations = 8;
constexpr size_t MaxPreconditions = 8;
constexpr size_t MaxConstraints = 8;
constexpr uint32_t MaxHorizonTicks = 36000;
constexpr uint8_t MaxPriority = 100;

bool ParseUint64(const std::string& text, uint64_t& value)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
    if (errno != 0 || !end || *end != '\0')
        return false;
    value = uint64_t(parsed);
    return true;
}

bool EnvelopeParseUint32(const std::string& text, uint32_t& value)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
    if (errno != 0 || !end || *end != '\0' || parsed > std::numeric_limits<uint32_t>::max())
        return false;
    value = uint32_t(parsed);
    return true;
}

bool EnvelopeParseInt32(const std::string& text, int32_t& value)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (errno != 0 || !end || *end != '\0' || parsed < std::numeric_limits<int32_t>::min() ||
        parsed > std::numeric_limits<int32_t>::max())
        return false;
    value = int32_t(parsed);
    return true;
}

std::vector<std::string> EnvelopeSplitCsv(const std::string& line)
{
    std::vector<std::string> parts;
    std::string current;
    for (const char ch : line)
    {
        if (ch == ',')
        {
            parts.push_back(current);
            current.clear();
        }
        else
        {
            current += ch;
        }
    }
    parts.push_back(current);
    return parts;
}

bool ParseScopeKind(const std::string& text, AgentScopeKind& kind)
{
    if (text == "zone")
        kind = AgentScopeKind::Zone;
    else if (text == "faction")
        kind = AgentScopeKind::Faction;
    else if (text == "ecology")
        kind = AgentScopeKind::Ecology;
    else if (text == "economy")
        kind = AgentScopeKind::Economy;
    else if (text == "incident")
        kind = AgentScopeKind::Incident;
    else if (text == "region")
        kind = AgentScopeKind::Region;
    else if (text == "squad")
        kind = AgentScopeKind::Squad;
    else if (text == "mutant_pack")
        kind = AgentScopeKind::MutantPack;
    else if (text == "named_actor")
        kind = AgentScopeKind::NamedActor;
    else
        return false;
    return true;
}

std::string ScopeKindToString(AgentScopeKind kind)
{
    switch (kind)
    {
    case AgentScopeKind::Zone: return "zone";
    case AgentScopeKind::Faction: return "faction";
    case AgentScopeKind::Ecology: return "ecology";
    case AgentScopeKind::Economy: return "economy";
    case AgentScopeKind::Incident: return "incident";
    case AgentScopeKind::Region: return "region";
    case AgentScopeKind::Squad: return "squad";
    case AgentScopeKind::MutantPack: return "mutant_pack";
    case AgentScopeKind::NamedActor: return "named_actor";
    }
    return "unknown";
}

bool ParseOpKind(const std::string& text, PlanOpKind& kind)
{
    if (text == "create")
        kind = PlanOpKind::Create;
    else if (text == "dissolve")
        kind = PlanOpKind::Dissolve;
    else if (text == "dispatch")
        kind = PlanOpKind::Dispatch;
    else if (text == "move")
        kind = PlanOpKind::Move;
    else if (text == "retreat")
        kind = PlanOpKind::Retreat;
    else if (text == "resupply")
        kind = PlanOpKind::Resupply;
    else if (text == "claim")
        kind = PlanOpKind::Claim;
    else if (text == "release")
        kind = PlanOpKind::Release;
    else if (text == "relate")
        kind = PlanOpKind::Relate;
    else if (text == "set_objective")
        kind = PlanOpKind::SetObjective;
    else if (text == "clear_objective")
        kind = PlanOpKind::ClearObjective;
    else if (text == "adjust_population")
        kind = PlanOpKind::AdjustPopulation;
    else if (text == "set_migration")
        kind = PlanOpKind::SetMigration;
    else if (text == "set_supply")
        kind = PlanOpKind::SetSupply;
    else if (text == "set_demand")
        kind = PlanOpKind::SetDemand;
    else if (text == "set_route")
        kind = PlanOpKind::SetRoute;
    else if (text == "propose_incident")
        kind = PlanOpKind::ProposeIncident;
    else
        return false;
    return true;
}

std::string OpKindToString(PlanOpKind kind)
{
    switch (kind)
    {
    case PlanOpKind::Create: return "create";
    case PlanOpKind::Dissolve: return "dissolve";
    case PlanOpKind::Dispatch: return "dispatch";
    case PlanOpKind::Move: return "move";
    case PlanOpKind::Retreat: return "retreat";
    case PlanOpKind::Resupply: return "resupply";
    case PlanOpKind::Claim: return "claim";
    case PlanOpKind::Release: return "release";
    case PlanOpKind::Relate: return "relate";
    case PlanOpKind::SetObjective: return "set_objective";
    case PlanOpKind::ClearObjective: return "clear_objective";
    case PlanOpKind::AdjustPopulation: return "adjust_population";
    case PlanOpKind::SetMigration: return "set_migration";
    case PlanOpKind::SetSupply: return "set_supply";
    case PlanOpKind::SetDemand: return "set_demand";
    case PlanOpKind::SetRoute: return "set_route";
    case PlanOpKind::ProposeIncident: return "propose_incident";
    }
    return "unknown";
}
} // namespace

IntentParseResult ParseWorldIntentEnvelope(const std::string& text)
{
    IntentParseResult result;
    if (text.size() > MaxEnvelopeBytes)
    {
        result.reason = "envelope_too_large";
        return result;
    }

    std::istringstream stream(text);
    std::string line;

    bool sawHeader = false;
    bool sawRequestId = false;
    bool sawAgentId = false;
    bool sawScope = false;
    bool sawBaseRevision = false;
    bool sawHorizonTicks = false;
    bool sawPriority = false;
    bool sawWakeReason = false;
    bool sawRationale = false;

    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;

        if (!sawHeader)
        {
            if (line != "xrsim_world_intent_v1")
            {
                result.reason = "unsupported_envelope_header";
                return result;
            }
            sawHeader = true;
            continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string::npos)
        {
            result.reason = "unknown_record";
            return result;
        }

        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (value.size() > MaxStringLength)
        {
            result.reason = "field_too_long";
            return result;
        }

        if (key == "request_id")
        {
            if (sawRequestId || !ParseUint64(value, result.envelope.requestId) || result.envelope.requestId == 0)
            {
                result.reason = sawRequestId ? "duplicate_request_id" : "invalid_request_id";
                return result;
            }
            sawRequestId = true;
        }
        else if (key == "agent_id")
        {
            if (sawAgentId || !EnvelopeParseUint32(value, result.envelope.agentId) || result.envelope.agentId == 0)
            {
                result.reason = sawAgentId ? "duplicate_agent_id" : "invalid_agent_id";
                return result;
            }
            sawAgentId = true;
        }
        else if (key == "scope")
        {
            if (sawScope)
            {
                result.reason = "duplicate_scope";
                return result;
            }
            const std::vector<std::string> parts = EnvelopeSplitCsv(value);
            if (parts.size() != 2)
            {
                result.reason = "invalid_scope";
                return result;
            }
            if (!ParseScopeKind(parts[0], result.envelope.scope.kind) ||
                !EnvelopeParseUint32(parts[1], result.envelope.scope.id))
            {
                result.reason = "invalid_scope";
                return result;
            }
            sawScope = true;
        }
        else if (key == "base_revision")
        {
            if (sawBaseRevision || !ParseUint64(value, result.envelope.baseRevision) || result.envelope.baseRevision == 0)
            {
                result.reason = sawBaseRevision ? "duplicate_base_revision" : "invalid_base_revision";
                return result;
            }
            sawBaseRevision = true;
        }
        else if (key == "horizon_ticks")
        {
            if (sawHorizonTicks || !EnvelopeParseUint32(value, result.envelope.horizonTicks) ||
                result.envelope.horizonTicks == 0 || result.envelope.horizonTicks > MaxHorizonTicks)
            {
                result.reason = sawHorizonTicks ? "duplicate_horizon_ticks" : "invalid_horizon_ticks";
                return result;
            }
            sawHorizonTicks = true;
        }
        else if (key == "priority")
        {
            uint32_t prio = 0;
            if (sawPriority || !EnvelopeParseUint32(value, prio) || prio > MaxPriority)
            {
                result.reason = sawPriority ? "duplicate_priority" : "invalid_priority";
                return result;
            }
            result.envelope.priority = uint8_t(prio);
            sawPriority = true;
        }
        else if (key == "wake_reason")
        {
            if (sawWakeReason || value.empty())
            {
                result.reason = sawWakeReason ? "duplicate_wake_reason" : "invalid_wake_reason";
                return result;
            }
            result.envelope.wakeReason = value;
            sawWakeReason = true;
        }
        else if (key == "rationale")
        {
            if (sawRationale || value.empty())
            {
                result.reason = sawRationale ? "duplicate_rationale" : "invalid_rationale";
                return result;
            }
            result.envelope.rationaleSummary = value;
            sawRationale = true;
        }
        else if (key == "precondition")
        {
            if (result.envelope.preconditions.size() >= MaxPreconditions)
            {
                result.reason = "precondition_limit_exceeded";
                return result;
            }
            result.envelope.preconditions.push_back(value);
        }
        else if (key == "constraint")
        {
            if (result.envelope.constraints.size() >= MaxConstraints)
            {
                result.reason = "constraint_limit_exceeded";
                return result;
            }
            result.envelope.constraints.push_back(value);
        }
        else if (key == "op")
        {
            if (result.envelope.operations.size() >= MaxOperations)
            {
                result.reason = "operation_limit_exceeded";
                return result;
            }
            const std::vector<std::string> parts = EnvelopeSplitCsv(value);
            if (parts.empty())
            {
                result.reason = "invalid_operation";
                return result;
            }
            PlanOp op;
            if (!ParseOpKind(parts[0], op.kind))
            {
                result.reason = "unknown_operation_kind";
                return result;
            }

            if (parts.size() >= 2)
                op.subject = parts[1];
            if (parts.size() >= 3)
                op.verb = parts[2];
            if (parts.size() >= 4)
                op.target = parts[3];

            if (parts.size() == 5)
            {
                if (op.kind == PlanOpKind::SetObjective || op.kind == PlanOpKind::Move ||
                    op.kind == PlanOpKind::Retreat || op.kind == PlanOpKind::Resupply ||
                    op.kind == PlanOpKind::Dispatch)
                {
                    if (!ParseUint64(parts[4], op.expiresAtTick))
                    {
                        result.reason = "invalid_operation_expiry";
                        return result;
                    }
                }
                else
                {
                    if (!EnvelopeParseInt32(parts[4], op.amount))
                    {
                        result.reason = "invalid_operation_amount";
                        return result;
                    }
                }
            }
            else if (parts.size() >= 6)
            {
                if (!EnvelopeParseInt32(parts[4], op.amount) || !ParseUint64(parts[5], op.expiresAtTick))
                {
                    result.reason = "invalid_operation_amount_or_expiry";
                    return result;
                }
            }

            result.envelope.operations.push_back(op);
        }
        else
        {
            result.reason = "unknown_record";
            return result;
        }
    }

    if (!sawHeader || !sawRequestId || !sawAgentId || !sawScope || !sawBaseRevision || !sawHorizonTicks ||
        !sawPriority || !sawWakeReason || !sawRationale)
    {
        result.reason = "missing_required_fields";
        return result;
    }

    result.ok = true;
    return result;
}

std::string FormatWorldIntentEnvelope(const IntentEnvelope& envelope)
{
    std::ostringstream out;
    out << "xrsim_world_intent_v1\n";
    out << "request_id=" << envelope.requestId << "\n";
    out << "agent_id=" << envelope.agentId << "\n";
    out << "scope=" << ScopeKindToString(envelope.scope.kind) << "," << envelope.scope.id << "\n";
    out << "base_revision=" << envelope.baseRevision << "\n";
    out << "horizon_ticks=" << envelope.horizonTicks << "\n";
    out << "priority=" << uint32_t(envelope.priority) << "\n";
    out << "wake_reason=" << envelope.wakeReason << "\n";
    out << "rationale=" << envelope.rationaleSummary << "\n";

    for (const auto& pre : envelope.preconditions)
        out << "precondition=" << pre << "\n";
    for (const auto& con : envelope.constraints)
        out << "constraint=" << con << "\n";

    for (const auto& op : envelope.operations)
    {
        out << "op=" << OpKindToString(op.kind);
        if (!op.subject.empty())
            out << "," << op.subject;
        if (!op.verb.empty())
            out << "," << op.verb;
        if (!op.target.empty())
            out << "," << op.target;
        if (op.kind == PlanOpKind::SetObjective || op.kind == PlanOpKind::Move || op.kind == PlanOpKind::Retreat ||
            op.kind == PlanOpKind::Resupply || op.kind == PlanOpKind::Dispatch)
        {
            out << "," << op.expiresAtTick;
        }
        else if (op.amount != 0 || op.expiresAtTick != 0)
        {
            out << "," << op.amount;
            if (op.expiresAtTick != 0)
                out << "," << op.expiresAtTick;
        }
        out << "\n";
    }

    return out.str();
}
} // namespace xrSim
