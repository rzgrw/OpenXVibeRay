#include "xrSim/xrSimAuthority.h"

#include <sstream>

namespace xrSim
{
bool operator==(const ScopeId& lhs, const ScopeId& rhs)
{
    return lhs.kind == rhs.kind && lhs.id == rhs.id;
}

bool operator<(const ScopeId& lhs, const ScopeId& rhs)
{
    if (lhs.kind != rhs.kind)
        return uint8_t(lhs.kind) < uint8_t(rhs.kind);
    return lhs.id < rhs.id;
}

const AuthorityRecord* AuthorityRegistry::Find(ScopeId scope) const
{
    for (const auto& r : m_records)
    {
        if (r.scope == scope)
            return &r;
    }
    return nullptr;
}

AuthorityRecord* AuthorityRegistry::FindMutable(ScopeId scope)
{
    for (auto& r : m_records)
    {
        if (r.scope == scope)
            return &r;
    }
    return nullptr;
}

void AuthorityRegistry::AppendAudit(
    ScopeId scope,
    AuthorityMode from,
    AuthorityMode to,
    ObjectiveAuthor author,
    uint32_t agentId,
    uint64_t revision,
    bool accepted,
    const std::string& reason)
{
    AuthorityAuditRecord record;
    record.seq = m_nextAuditSeq++;
    record.revision = revision;
    record.scope = scope;
    record.from = from;
    record.to = to;
    record.attemptedAuthor = author;
    record.attemptedAgentId = agentId;
    record.accepted = accepted;
    record.reason = reason;
    m_audit.push_back(record);
}

AuthorityResult AuthorityRegistry::Register(ScopeId scope, uint32_t ownerAgentId, uint64_t revision)
{
    if (ownerAgentId == 0)
        return AuthorityResult{ false, "invalid_agent_id" };
    if (Find(scope))
        return AuthorityResult{ false, "duplicate_scope" };

    AuthorityRecord record;
    record.scope = scope;
    record.ownerAgentId = ownerAgentId;
    record.mode = AuthorityMode::Observe;
    record.revision = revision;
    record.activePlanId = 0;
    record.expiresAtTick = 0;
    m_records.push_back(record);

    AppendAudit(scope, AuthorityMode::Observe, AuthorityMode::Observe, ObjectiveAuthor::Legacy, ownerAgentId, revision,
        true, "registered");
    return AuthorityResult{ true, "" };
}

AuthorityResult AuthorityRegistry::Transition(
    ScopeId scope, AuthorityMode expected, AuthorityMode next, uint64_t revision)
{
    AuthorityRecord* r = FindMutable(scope);
    if (!r)
        return AuthorityResult{ false, "scope_not_found" };
    if (r->mode != expected)
        return AuthorityResult{ false, "invalid_authority_transition" };

    if (expected == AuthorityMode::TransferPending && next == AuthorityMode::AiOwned)
    {
        if (revision != r->revision)
        {
            AppendAudit(scope, r->mode, r->mode, ObjectiveAuthor::Ai, r->ownerAgentId, revision, false,
                "transfer_revision_mismatch");
            return AuthorityResult{ false, "transfer_revision_mismatch" };
        }
    }

    const AuthorityMode from = r->mode;
    r->mode = next;
    r->revision = revision;
    AppendAudit(scope, from, next, ObjectiveAuthor::Ai, r->ownerAgentId, revision, true, "transition");
    return AuthorityResult{ true, "" };
}

AuthorityResult AuthorityRegistry::BeginShadow(ScopeId scope, uint64_t revision)
{
    return Transition(scope, AuthorityMode::Observe, AuthorityMode::Shadow, revision);
}

AuthorityResult AuthorityRegistry::BeginTransfer(ScopeId scope, uint64_t revision)
{
    return Transition(scope, AuthorityMode::Shadow, AuthorityMode::TransferPending, revision);
}

AuthorityResult AuthorityRegistry::CommitTransfer(ScopeId scope, uint64_t revision)
{
    return Transition(scope, AuthorityMode::TransferPending, AuthorityMode::AiOwned, revision);
}

AuthorityResult AuthorityRegistry::ReverseTransfer(ScopeId scope, uint64_t revision)
{
    AuthorityRecord* r = FindMutable(scope);
    if (!r)
        return AuthorityResult{ false, "scope_not_found" };
    if (r->mode != AuthorityMode::AiOwned)
        return AuthorityResult{ false, "invalid_authority_transition" };
    if (revision != r->revision)
    {
        AppendAudit(scope, r->mode, r->mode, ObjectiveAuthor::Legacy, 0, revision, false,
            "reverse_revision_mismatch");
        return AuthorityResult{ false, "reverse_revision_mismatch" };
    }

    r->mode = AuthorityMode::Observe;
    r->revision = revision;
    AppendAudit(scope, AuthorityMode::AiOwned, AuthorityMode::Observe, ObjectiveAuthor::Legacy, 0, revision, true,
        "reversed");
    return AuthorityResult{ true, "" };
}

AuthorityResult AuthorityRegistry::CanAuthor(ScopeId scope, ObjectiveAuthor author, uint32_t agentId)
{
    const AuthorityRecord* r = Find(scope);
    if (!r)
        return AuthorityResult{ false, "scope_not_found" };

    if (r->mode == AuthorityMode::Observe || r->mode == AuthorityMode::Shadow)
    {
        if (author == ObjectiveAuthor::Legacy)
            return AuthorityResult{ true, "" };

        AppendAudit(scope, r->mode, r->mode, author, agentId, r->revision, false, "ai_author_not_transferred");
        return AuthorityResult{ false, "ai_author_not_transferred" };
    }

    if (r->mode == AuthorityMode::TransferPending)
    {
        AppendAudit(scope, r->mode, r->mode, author, agentId, r->revision, false, "transfer_in_progress");
        return AuthorityResult{ false, "transfer_in_progress" };
    }

    if (r->mode == AuthorityMode::AiOwned)
    {
        if (author == ObjectiveAuthor::Legacy)
        {
            AppendAudit(scope, r->mode, r->mode, author, agentId, r->revision, false, "legacy_author_rejected");
            return AuthorityResult{ false, "legacy_author_rejected" };
        }
        if (author == ObjectiveAuthor::Ai)
        {
            if (agentId == r->ownerAgentId)
                return AuthorityResult{ true, "" };

            AppendAudit(scope, r->mode, r->mode, author, agentId, r->revision, false, "unauthorized_agent");
            return AuthorityResult{ false, "unauthorized_agent" };
        }
        return AuthorityResult{ true, "" };
    }

    return AuthorityResult{ false, "unknown_authority_mode" };
}

AuthorityResult AuthorityRegistry::RecordPlan(
    ScopeId scope, uint32_t agentId, uint64_t planId, uint64_t expiresAtTick, uint64_t revision)
{
    if (planId == 0)
        return AuthorityResult{ false, "invalid_plan_id" };

    const AuthorityResult can = CanAuthor(scope, ObjectiveAuthor::Ai, agentId);
    if (!can.ok)
        return can;

    AuthorityRecord* r = FindMutable(scope);
    if (!r)
        return AuthorityResult{ false, "scope_not_found" };

    r->activePlanId = planId;
    r->expiresAtTick = expiresAtTick;
    r->revision = revision;
    AppendAudit(scope, r->mode, r->mode, ObjectiveAuthor::Ai, agentId, revision, true, "plan_recorded");
    return AuthorityResult{ true, "" };
}

const std::vector<AuthorityAuditRecord>& AuthorityRegistry::Audit() const
{
    return m_audit;
}

std::string AuthorityRegistry::DumpAudit() const
{
    std::ostringstream out;
    out << "audit_records " << m_audit.size() << "\n";
    for (const auto& a : m_audit)
    {
        out << "seq=" << a.seq << " rev=" << a.revision << " scope=" << uint32_t(a.scope.kind) << ":" << a.scope.id
            << " from=" << uint32_t(a.from) << " to=" << uint32_t(a.to) << " author=" << uint32_t(a.attemptedAuthor)
            << " agent=" << a.attemptedAgentId << " accepted=" << (a.accepted ? 1 : 0)
            << " reason=" << a.reason << "\n";
    }
    return out.str();
}
} // namespace xrSim
