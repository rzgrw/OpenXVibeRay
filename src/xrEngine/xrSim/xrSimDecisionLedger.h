#pragma once

#include "xrSim/xrSimIntentCommit.h"

#include <cstdint>
#include <string>
#include <vector>

namespace xrSim
{
enum class ProviderVerdict : uint8_t
{
    Accepted,
    Coast,
    Timeout,
    RateLimited,
    TransportError,
    Cancelled,
    CodecRejected,
    ValidationRejected,
    StaleRejected
};

struct TokenUsage
{
    uint32_t inputTokens = 0;
    uint32_t outputTokens = 0;
};

struct RequestLedgerRecord
{
    uint64_t seq = 0;
    uint64_t requestId = 0;
    uint32_t agentId = 0;
    ScopeId scope;
    uint64_t baseRevision = 0;
    std::string wakeReason;
    std::string promptHash;
    std::string responseHash;
    std::string provider;
    std::string model;
    TokenUsage usage;
    uint32_t latencyMs = 0;
    uint32_t retryCount = 0;
    ProviderVerdict providerVerdict = ProviderVerdict::Coast;
    std::string codecVerdict;
    std::string validationVerdict;
    uint64_t committedRevision = 0;
    std::vector<AppliedDelta> deltas;
    std::vector<uint64_t> eventSeqs;
};

class DecisionLedger
{
public:
    void AppendRequest(RequestLedgerRecord record);
    const std::vector<RequestLedgerRecord>& Requests() const;
    std::string Dump() const;
    std::string DumpPage(uint64_t afterSeq, uint32_t limit) const;
    std::string SaveReplayPacket() const;

private:
    uint64_t m_nextSeq = 1;
    std::vector<RequestLedgerRecord> m_requests;
};

std::string StableLedgerHash(const std::string& value);
uint64_t StableLedgerHash64(const std::string& value);
} // namespace xrSim
