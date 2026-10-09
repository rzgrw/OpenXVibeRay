#pragma once

#include "xrSim/xrSimZoneState.h"

#include <cstdint>
#include <string>
#include <vector>

namespace xrSim
{
enum class AgentScopeKind : uint8_t
{
    Zone,
    Faction,
    Ecology,
    Economy,
    Incident,
    Region,
    Squad,
    MutantPack,
    NamedActor
};

struct ScopeId
{
    AgentScopeKind kind = AgentScopeKind::Zone;
    uint32_t id = 0;
};

bool operator==(const ScopeId& lhs, const ScopeId& rhs);
bool operator<(const ScopeId& lhs, const ScopeId& rhs);

enum class AuthorityMode : uint8_t
{
    Observe,
    Shadow,
    TransferPending,
    AiOwned
};

struct AuthorityRecord
{
    ScopeId scope;
    uint32_t ownerAgentId = 0;
    AuthorityMode mode = AuthorityMode::Observe;
    uint64_t revision = 0;
    uint64_t activePlanId = 0;
    uint64_t expiresAtTick = 0;
};

struct AuthorityResult
{
    bool ok = false;
    std::string reason;
};

struct AuthorityAuditRecord
{
    uint64_t seq = 0;
    uint64_t revision = 0;
    ScopeId scope;
    AuthorityMode from = AuthorityMode::Observe;
    AuthorityMode to = AuthorityMode::Observe;
    ObjectiveAuthor attemptedAuthor = ObjectiveAuthor::Legacy;
    uint32_t attemptedAgentId = 0;
    bool accepted = false;
    std::string reason;
};

class AuthorityRegistry
{
public:
    AuthorityResult Register(ScopeId scope, uint32_t ownerAgentId, uint64_t revision);
    AuthorityResult BeginShadow(ScopeId scope, uint64_t revision);
    AuthorityResult BeginTransfer(ScopeId scope, uint64_t revision);
    AuthorityResult CommitTransfer(ScopeId scope, uint64_t revision);
    AuthorityResult ReverseTransfer(ScopeId scope, uint64_t revision);
    AuthorityResult CanAuthor(ScopeId scope, ObjectiveAuthor author, uint32_t agentId);
    AuthorityResult RecordPlan(ScopeId scope, uint32_t agentId, uint64_t planId,
        uint64_t expiresAtTick, uint64_t revision);
    const AuthorityRecord* Find(ScopeId scope) const;
    const std::vector<AuthorityAuditRecord>& Audit() const;
    std::string DumpAudit() const;

private:
    AuthorityRecord* FindMutable(ScopeId scope);
    AuthorityResult Transition(ScopeId scope, AuthorityMode expected, AuthorityMode next,
        uint64_t revision);
    void AppendAudit(ScopeId scope, AuthorityMode from, AuthorityMode to,
        ObjectiveAuthor author, uint32_t agentId, uint64_t revision, bool accepted,
        const std::string& reason);
    uint64_t m_nextAuditSeq = 1;
    std::vector<AuthorityRecord> m_records;
    std::vector<AuthorityAuditRecord> m_audit;
};
} // namespace xrSim
