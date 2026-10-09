#include "xrSim/xrSimDecisionLedger.h"

#include <cstdio>
#include <iomanip>
#include <sstream>

namespace xrSim
{
uint64_t StableLedgerHash64(const std::string& value)
{
    uint64_t hash = 14695981039346656037ULL;
    for (const char c : value)
    {
        hash ^= uint64_t(static_cast<unsigned char>(c));
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string StableLedgerHash(const std::string& value)
{
    const uint64_t hash = StableLedgerHash64(value);
    char buf[17]{};
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(hash));
    return std::string(buf);
}

void DecisionLedger::AppendRequest(RequestLedgerRecord record)
{
    if (record.seq == 0)
        record.seq = m_nextSeq++;
    else if (record.seq >= m_nextSeq)
        m_nextSeq = record.seq + 1;
    m_requests.push_back(std::move(record));
}

const std::vector<RequestLedgerRecord>& DecisionLedger::Requests() const
{
    return m_requests;
}

std::string DecisionLedger::Dump() const
{
    std::ostringstream out;
    out << "ledger_requests " << m_requests.size() << "\n";
    for (const auto& r : m_requests)
    {
        out << "seq=" << r.seq << " req=" << r.requestId << " agent=" << r.agentId
            << " scope=" << uint32_t(r.scope.kind) << ":" << r.scope.id
            << " base_rev=" << r.baseRevision << " p_hash=" << r.promptHash
            << " r_hash=" << r.responseHash << " provider=" << r.provider
            << " model=" << r.model << " in_tok=" << r.usage.inputTokens
            << " out_tok=" << r.usage.outputTokens << " latency_ms=" << r.latencyMs
            << " retry=" << r.retryCount << " verdict=" << uint32_t(r.providerVerdict)
            << " codec=" << r.codecVerdict << " validation=" << r.validationVerdict
            << " committed_rev=" << r.committedRevision << "\n";
    }
    return out.str();
}

namespace
{
const char* ProviderVerdictName(ProviderVerdict verdict)
{
    switch (verdict)
    {
    case ProviderVerdict::Accepted: return "accepted";
    case ProviderVerdict::Coast: return "coast";
    case ProviderVerdict::Timeout: return "timeout";
    case ProviderVerdict::RateLimited: return "rate_limited";
    case ProviderVerdict::TransportError: return "transport_error";
    case ProviderVerdict::Cancelled: return "cancelled";
    case ProviderVerdict::CodecRejected: return "codec_rejected";
    case ProviderVerdict::ValidationRejected: return "validation_rejected";
    case ProviderVerdict::StaleRejected: return "stale_rejected";
    }
    return "unknown";
}
} // namespace

std::string DecisionLedger::DumpPage(uint64_t afterSeq, uint32_t limit) const
{
    std::ostringstream out;
    uint32_t count = 0;
    uint64_t lastSeq = afterSeq;
    bool more = false;
    for (const auto& r : m_requests)
    {
        if (r.seq <= afterSeq)
            continue;
        if (limit > 0 && count >= limit)
        {
            more = true;
            break;
        }

        out << "seq=" << r.seq << " req=" << r.requestId << " agent=" << r.agentId
            << " scope=" << uint32_t(r.scope.kind) << ":" << r.scope.id
            << " base_rev=" << r.baseRevision << " wake=" << r.wakeReason
            << " p_hash=" << r.promptHash << " r_hash=" << r.responseHash
            << " prov=" << r.provider << " model=" << r.model
            << " verdict=" << uint32_t(r.providerVerdict) << ":" << ProviderVerdictName(r.providerVerdict)
            << " codec=" << r.codecVerdict << " validation=" << r.validationVerdict
            << " committed_rev=" << r.committedRevision << " deltas=" << r.deltas.size() << "\n";
        lastSeq = r.seq;
        ++count;
    }
    out << "next=" << lastSeq << " done=" << (more ? 0 : 1);
    return out.str();
}

std::string DecisionLedger::SaveReplayPacket() const
{
    std::ostringstream out;
    out << "xrsim_decision_ledger_v1\n";
    out << "count " << m_requests.size() << "\n";
    for (const auto& r : m_requests)
    {
        out << "record " << r.seq << " " << r.requestId << " " << r.agentId << " "
            << uint32_t(r.scope.kind) << " " << r.scope.id << " " << r.baseRevision << " "
            << r.promptHash << " " << r.responseHash << " " << r.provider << " " << r.model << " "
            << r.usage.inputTokens << " " << r.usage.outputTokens << " " << r.latencyMs << " "
            << uint32_t(r.providerVerdict) << " " << r.committedRevision << "\n";
    }
    out << "end\n";
    return out.str();
}
} // namespace xrSim
