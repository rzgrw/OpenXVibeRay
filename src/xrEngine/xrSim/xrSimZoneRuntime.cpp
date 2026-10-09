#include "xrSim/xrSimZoneRuntime.h"

#include <algorithm>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>

namespace xrSim
{
namespace
{
AgentScopeKind ZoneRuntimeParseScopeKindString(const std::string& text)
{
    if (text == "zone") return AgentScopeKind::Zone;
    if (text == "faction") return AgentScopeKind::Faction;
    if (text == "ecology") return AgentScopeKind::Ecology;
    if (text == "economy") return AgentScopeKind::Economy;
    if (text == "incident") return AgentScopeKind::Incident;
    if (text == "region") return AgentScopeKind::Region;
    if (text == "squad") return AgentScopeKind::Squad;
    if (text == "mutant_pack") return AgentScopeKind::MutantPack;
    if (text == "named_actor") return AgentScopeKind::NamedActor;
    return AgentScopeKind::Zone;
}
} // namespace

struct ZoneRuntime::Impl
{
    ZoneState world;
    AuthorityRegistry authority;
    DecisionLedger ledger;
    std::vector<WorldAgentRecord> agents;
    std::unique_ptr<Materializer> materializer;
    std::unique_ptr<WakeScheduler> scheduler;
    ZoneRuntimeConfig config;
    ZoneRuntimeMetrics metrics;

    bool bootstrapped = false;
    bool running = false;
    uint64_t nextTickMs = 0;
    uint64_t simulationTick = 0;
    uint64_t nextRequestId = 1;

    struct LiveEventInboxItem
    {
        uint16_t targetId = 0;
        uint16_t sourceId = 0;
        uint32_t damagePermille = 0;
        bool isDeath = false;
    };
    std::deque<LiveEventInboxItem> liveInbox;
    std::mutex inboxMutex;
};

ZoneRuntime::ZoneRuntime() : m_impl(std::make_unique<Impl>()) {}
ZoneRuntime::~ZoneRuntime() = default;

ZoneRuntimeResult ZoneRuntime::Bootstrap(
    const ZoneManifest& manifest,
    IAgentZoneLiveAdapter& adapter,
    const ZoneRuntimeConfig& config,
    std::unique_ptr<IWorldIntentProvider> provider)
{
    m_impl->config = config;
    const ZoneResult zoneRes = m_impl->world.Bootstrap(manifest);
    if (!zoneRes.ok)
        return ZoneRuntimeResult{ false, "world_bootstrap_failed: " + zoneRes.reason };

    for (const auto& a : manifest.agents)
    {
        const ScopeId scope{ ZoneRuntimeParseScopeKindString(a.scopeKind), a.scopeId };
        m_impl->authority.Register(scope, a.agentId, m_impl->world.Revision());
    }

    m_impl->agents = CreateWorldAgents(manifest);
    m_impl->materializer = std::make_unique<Materializer>(adapter);
    m_impl->scheduler = std::make_unique<WakeScheduler>(config.scheduler, std::move(provider));
    m_impl->bootstrapped = true;
    m_impl->running = true;

    return ZoneRuntimeResult{ true, "" };
}

ZoneRuntimeResult ZoneRuntime::Start()
{
    if (!m_impl->bootstrapped)
        return ZoneRuntimeResult{ false, "runtime_not_bootstrapped" };
    m_impl->running = true;
    return ZoneRuntimeResult{ true, "" };
}

void ZoneRuntime::Reset()
{
    if (m_impl->scheduler)
        m_impl->scheduler->CancelAll();
    if (m_impl->materializer)
        m_impl->materializer->ReleaseAll();

    m_impl->bootstrapped = false;
    m_impl->running = false;
    m_impl->nextTickMs = 0;
    m_impl->simulationTick = 0;
}

void ZoneRuntime::NotifyHit(uint16_t targetId, uint16_t sourceId, uint32_t damagePermille)
{
    std::lock_guard lock(m_impl->inboxMutex);
    if (m_impl->liveInbox.size() >= 256)
    {
        ++m_impl->metrics.liveEventsDropped;
        return;
    }
    Impl::LiveEventInboxItem item;
    item.targetId = targetId;
    item.sourceId = sourceId;
    item.damagePermille = damagePermille;
    item.isDeath = false;
    m_impl->liveInbox.push_back(item);
}

void ZoneRuntime::NotifyDeath(uint16_t targetId, uint16_t sourceId)
{
    std::lock_guard lock(m_impl->inboxMutex);
    if (m_impl->liveInbox.size() >= 256)
    {
        ++m_impl->metrics.liveEventsDropped;
        return;
    }
    Impl::LiveEventInboxItem item;
    item.targetId = targetId;
    item.sourceId = sourceId;
    item.damagePermille = 1000;
    item.isDeath = true;
    m_impl->liveInbox.push_back(item);
}

const ZoneState& ZoneRuntime::World() const { return m_impl->world; }
const AuthorityRegistry& ZoneRuntime::Authority() const { return m_impl->authority; }
const DecisionLedger& ZoneRuntime::Ledger() const { return m_impl->ledger; }
const std::vector<WorldAgentRecord>& ZoneRuntime::Agents() const { return m_impl->agents; }
const ZoneRuntimeMetrics& ZoneRuntime::Metrics() const { return m_impl->metrics; }
WakeScheduler& ZoneRuntime::Scheduler() { return *m_impl->scheduler; }

void ZoneRuntime::TransferAllForTest()
{
    m_impl->world.SetRevision(3);
    for (const auto& a : m_impl->agents)
    {
        m_impl->authority.BeginShadow(a.scope, 2);
        m_impl->authority.BeginTransfer(a.scope, 3);
        m_impl->authority.CommitTransfer(a.scope, 3);
    }
}

void ZoneRuntime::WakeAgentForTest(
    uint32_t agentId, const std::string& reason, WakePriority priority, uint64_t nowMs)
{
    for (auto& a : m_impl->agents)
    {
        if (a.agentId == agentId)
        {
            const uint64_t reqId = m_impl->nextRequestId++;
            const WorldWakeContext ctx =
                PrepareWorldWake(reqId, m_impl->world, a, m_impl->simulationTick, reason);
            m_impl->scheduler->Schedule(ctx, priority, nowMs);
            return;
        }
    }
}

void ZoneRuntime::SetWorldRevisionForTest(uint64_t revision)
{
    m_impl->world.SetRevision(revision);
}

void ZoneRuntime::OnFrame(uint64_t nowMs, uint64_t frameNumber)
{
    (void)frameNumber;
    ++m_impl->metrics.frameCalls;

    if (!m_impl->running || !m_impl->bootstrapped)
        return;

    if (m_impl->scheduler)
        m_impl->scheduler->Pump(nowMs);

    const uint64_t tickMs = m_impl->config.tickMs ? m_impl->config.tickMs : 250;
    if (m_impl->nextTickMs == 0)
        m_impl->nextTickMs = (nowMs / tickMs) * tickMs + tickMs;

    if (nowMs < m_impl->nextTickMs)
        return;

    m_impl->nextTickMs += tickMs;
    ++m_impl->metrics.simulationTicks;
    ++m_impl->simulationTick;

    // 1. Drain live inbox
    std::deque<Impl::LiveEventInboxItem> inboxItems;
    {
        std::lock_guard lock(m_impl->inboxMutex);
        inboxItems.swap(m_impl->liveInbox);
    }

    for (const auto& item : inboxItems)
    {
        WorldEvent ev;
        ev.tick = m_impl->simulationTick;
        ev.kind = item.isDeath ? "live_death" : "live_hit";
        ev.amount = int32_t(item.damagePermille);
        ev.source = "live_observation";
        m_impl->world.AppendEvent(ev);
        ++m_impl->metrics.observedEvents;
    }

    // 2. Poll observations from materializer
    if (m_impl->materializer)
    {
        ObservationBatch batch = m_impl->materializer->PollObservations(m_impl->world, m_impl->simulationTick);
        for (const auto& ev : batch.events)
        {
            m_impl->world.AppendEvent(ev);
            ++m_impl->metrics.observedEvents;
        }
    }

    // 3. Expire objectives
    for (auto& obj : m_impl->world.MutableObjectives())
    {
        if (obj.active && obj.expiresAtTick > 0 && m_impl->simulationTick >= obj.expiresAtTick)
            obj.active = false;
    }

    // 4. Pump scheduler
    if (m_impl->scheduler)
        m_impl->scheduler->Pump(nowMs);

    // 5. Drain completed responses and commit valid intents
    if (m_impl->scheduler)
    {
        std::vector<SchedulerCompletion> ready = m_impl->scheduler->DrainReady();
        for (auto& comp : ready)
        {
            ++m_impl->metrics.providerRequests;
            m_impl->metrics.totalLatencyMs += comp.result.latencyMs;
            m_impl->metrics.inputTokens += comp.result.usage.inputTokens;
            m_impl->metrics.outputTokens += comp.result.usage.outputTokens;

            if (comp.result.ok)
            {
                auto stagedRes = StageIntent(m_impl->world, m_impl->authority, comp.result.intent, m_impl->simulationTick);
                if (stagedRes.ok)
                {
                    auto commitRes =
                        CommitStagedIntent(m_impl->world, m_impl->authority, std::move(stagedRes.staged), m_impl->simulationTick);
                    if (commitRes.ok)
                    {
                        ++m_impl->metrics.acceptedIntents;
                        RequestLedgerRecord rec;
                        rec.requestId = comp.requestId;
                        rec.agentId = comp.agentId;
                        rec.scope = comp.context.agent.scope;
                        rec.baseRevision = comp.context.baseRevision;
                        rec.wakeReason = comp.context.wakeReason;
                        rec.promptHash = StableLedgerHash(comp.context.prompt);
                        rec.responseHash = comp.result.responseHash;
                        rec.provider = comp.result.provider;
                        rec.model = comp.result.model;
                        rec.usage = comp.result.usage;
                        rec.latencyMs = comp.result.latencyMs;
                        rec.retryCount = comp.retryCount;
                        rec.providerVerdict = ProviderVerdict::Accepted;
                        rec.committedRevision = commitRes.committedRevision;
                        rec.deltas = commitRes.deltas;
                        m_impl->ledger.AppendRequest(rec);
                    }
                    else if (commitRes.stale)
                    {
                        ++m_impl->metrics.staleRejected;
                        ++m_impl->metrics.freshWakesAfterStale;
                        const uint64_t newReq = m_impl->nextRequestId++;
                        const WorldWakeContext freshCtx = PrepareWorldWake(
                            newReq, m_impl->world, comp.context.agent, m_impl->simulationTick, "stale_refresh");
                        m_impl->scheduler->Schedule(freshCtx, WakePriority::InvalidatedPlan, nowMs);
                    }
                }
                else
                {
                    if (stagedRes.reason == "stale_revision")
                    {
                        ++m_impl->metrics.staleRejected;
                        ++m_impl->metrics.freshWakesAfterStale;
                        const uint64_t newReq = m_impl->nextRequestId++;
                        const WorldWakeContext freshCtx = PrepareWorldWake(
                            newReq, m_impl->world, comp.context.agent, m_impl->simulationTick, "stale_refresh");
                        m_impl->scheduler->Schedule(freshCtx, WakePriority::InvalidatedPlan, nowMs);
                    }
                    else
                    {
                        ++m_impl->metrics.validationRejected;
                    }
                }
            }
            else if (comp.result.coast)
            {
                ++m_impl->metrics.providerCoasts;
            }
            else
            {
                ++m_impl->metrics.codecRejected;
            }
        }
    }

    // 6. Materialize committed objectives
    if (m_impl->materializer)
        m_impl->materializer->ApplyCommittedState(m_impl->world, m_impl->config.commandBudget, m_impl->simulationTick);
}

std::string ZoneRuntime::HandleBridgeVerb(
    const std::string& verb, const std::string& payload, bool& ok)
{
    ok = true;
    if (verb == "agent.zone.bootstrap")
    {
        if (payload.empty() || payload.size() > 1024)
        {
            ok = false;
            return "invalid_manifest_path";
        }
        std::ifstream file(payload);
        if (!file.is_open())
        {
            ok = false;
            return "cannot_open_manifest: " + payload;
        }
        std::ostringstream ss;
        ss << file.rdbuf();
        const ZoneManifestParseResult parsed = ParseZoneManifest(ss.str());
        if (!parsed.ok)
        {
            ok = false;
            return "manifest_parse_failed: " + parsed.reason;
        }

        m_impl->world = ZoneState();
        const ZoneResult bRes = m_impl->world.Bootstrap(parsed.manifest);
        if (!bRes.ok)
        {
            ok = false;
            return "bootstrap_failed: " + bRes.reason;
        }

        m_impl->authority = AuthorityRegistry();
        for (const auto& a : parsed.manifest.agents)
        {
            const ScopeId scope{ ZoneRuntimeParseScopeKindString(a.scopeKind), a.scopeId };
            m_impl->authority.Register(scope, a.agentId, 1);
        }
        m_impl->agents = CreateWorldAgents(parsed.manifest);
        m_impl->world.SetRevision(1);
        m_impl->bootstrapped = true;

        std::ostringstream out;
        out << "scenario=" << parsed.manifest.scenario
            << " revision=1 groups=" << parsed.manifest.groups.size();
        return out.str();
    }
    if (verb == "agent.zone.start")
    {
        if (!m_impl->bootstrapped)
        {
            ok = false;
            return "not_bootstrapped";
        }
        const AgentProviderConfig pcfg = LoadAgentProviderConfigFromEnvironment();
        m_impl->running = true;
        return "running provider=" + pcfg.provider + " model=" + pcfg.model;
    }
    if (verb == "agent.zone.shadow all")
    {
        if (!m_impl->bootstrapped)
        {
            ok = false;
            return "not_bootstrapped";
        }
        for (const auto& a : m_impl->agents)
            m_impl->authority.BeginShadow(a.scope, 2);
        m_impl->world.SetRevision(2);
        std::ostringstream out;
        out << "shadowed=" << m_impl->agents.size() << " revision=2";
        return out.str();
    }
    if (verb == "agent.zone.transfer all")
    {
        if (!m_impl->bootstrapped)
        {
            ok = false;
            return "not_bootstrapped";
        }
        for (const auto& a : m_impl->agents)
        {
            m_impl->authority.BeginTransfer(a.scope, 3);
            m_impl->authority.CommitTransfer(a.scope, 3);
        }
        m_impl->world.SetRevision(3);
        std::ostringstream out;
        out << "transferred=" << m_impl->agents.size() << " revision=3";
        return out.str();
    }
    if (verb == "agent.zone.agents")
    {
        std::ostringstream out;
        for (size_t i = 0; i < m_impl->agents.size(); ++i)
        {
            const auto& a = m_impl->agents[i];
            if (i != 0)
                out << "\n";
            out << "id=" << a.agentId << " scope=" << uint32_t(a.scope.kind) << ":" << a.scope.id
                << " name=" << a.name << " charter_v=" << a.charterVersion;
        }
        return out.str();
    }
    if (verb == "agent.zone.ledger")
    {
        std::istringstream in(payload);
        uint64_t afterSeq = 0;
        uint32_t limit = 50;
        in >> afterSeq >> limit;
        if (limit < 1 || limit > 256)
            limit = 50;
        return m_impl->ledger.DumpPage(afterSeq, limit);
    }
    if (verb == "agent.zone.wake")
    {
        std::istringstream in(payload);
        uint32_t agentId = 0;
        std::string reason;
        if (!(in >> agentId >> reason))
        {
            ok = false;
            return "usage: agent.zone.wake <agent_id> <reason>";
        }
        const uint64_t reqId = m_impl->nextRequestId++;
        WakeAgentForTest(agentId, reason, WakePriority::NearPlayer, m_impl->nextTickMs);
        return "request=" + std::to_string(reqId);
    }
    if (verb == "agent.zone.status")
    {
        std::ostringstream out;
        out << "running=" << (m_impl->running ? 1 : 0)
            << " tick=" << m_impl->simulationTick
            << " revision=" << m_impl->world.Revision()
            << " agents=" << m_impl->agents.size();
        return out.str();
    }
    if (verb == "agent.zone.authority")
        return m_impl->authority.DumpAudit();
    if (verb == "agent.zone.snapshot")
        return m_impl->world.SaveSnapshot();
    if (verb == "agent.zone.metrics")
    {
        std::ostringstream out;
        out << "frame_calls=" << m_impl->metrics.frameCalls
            << " simulation_ticks=" << m_impl->metrics.simulationTicks
            << " provider_wait_frame_us=0"
            << " accepted_intents=" << m_impl->metrics.acceptedIntents
            << " provider_coasts=" << m_impl->metrics.providerCoasts
            << " stale_rejected=" << m_impl->metrics.staleRejected
            << " observed_events=" << m_impl->metrics.observedEvents;
        return out.str();
    }
    if (verb == "agent.zone.stop")
    {
        m_impl->running = false;
        return "stopped";
    }
    if (verb == "agent.zone.reset")
    {
        Reset();
        return "reset cancelled=0";
    }

    ok = false;
    return "unknown_zone_verb: " + verb;
}
} // namespace xrSim
