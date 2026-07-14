#include "xrSim/xrSimAgentProvider.h"
#include "xrSim/xrSimAnthropicTransport.h"
#include "xrSim/xrSimActuator.h"
#include "xrSim/xrSimActorIntent.h"
#include "xrSim/xrSimActorProvider.h"
#include "xrSim/xrSimActorRuntime.h"
#include "xrSim/xrSimActors.h"
#include "xrSim/xrSimWorldState.h"
#include "xrSim/xrSimBridge.h"
#include "xrSim/xrSimNullAgent.h"
#include "xrSim/xrSimPackSpawn.h"

#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
class FakeAnthropicTransport : public xrSim::IAnthropicTransport
{
public:
    xrSim::AnthropicTransportResult Send(
        const xrSim::AgentProviderConfig& config, const xrSim::AnthropicMessagesRequest& request) override
    {
        seenModel = config.model;
        seenPath = request.path;
        seenBody = request.body;
        ++calls;
        return result;
    }

    xrSim::AnthropicTransportResult result;
    std::string seenModel;
    std::string seenPath;
    std::string seenBody;
    uint32_t calls = 0;
};

class BlockingActorProvider : public xrSim::IActorIntentProvider
{
public:
    xrSim::ActorProviderResult Wake(const xrSim::ActorWakeContext& context) override
    {
        {
            std::lock_guard guard{m_mutex};
            m_entered = true;
            seenAgentId = context.actor.agentId;
        }
        m_cv.notify_all();

        std::unique_lock lock{m_mutex};
        m_cv.wait(lock, [this] { return m_released || m_cancelled; });

        xrSim::ActorProviderResult result;
        if (m_cancelled)
        {
            result.ok = true;
            result.coast = true;
            result.provider = "blocking-fixture";
            result.model = "fixture";
            result.coastReason = "cancelled";
            result.plan.coast = true;
            return result;
        }

        result.ok = true;
        result.provider = "blocking-fixture";
        result.model = "fixture";
        result.plan.goal = "queued_pack_goal";
        return result;
    }

    void Cancel() override
    {
        {
            std::lock_guard guard{m_mutex};
            m_cancelled = true;
        }
        m_cv.notify_all();
    }

    bool WaitUntilEntered()
    {
        std::unique_lock lock{m_mutex};
        return m_cv.wait_for(lock, std::chrono::seconds(1), [this] { return m_entered; });
    }

    void Release()
    {
        {
            std::lock_guard guard{m_mutex};
            m_released = true;
        }
        m_cv.notify_all();
    }

    uint32_t seenAgentId = 0;

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_entered = false;
    bool m_released = false;
    bool m_cancelled = false;
};

class ScopedEnvVar
{
public:
    ScopedEnvVar(const char* name, const char* value) : m_name(name)
    {
        const char* previous = std::getenv(name);
        if (previous)
        {
            m_hadPrevious = true;
            m_previous = previous;
        }
        Set(value);
    }

    ~ScopedEnvVar()
    {
        if (m_hadPrevious)
            Set(m_previous.c_str());
        else
            Unset();
    }

private:
    void Set(const char* value)
    {
#if defined(_WIN32)
        _putenv_s(m_name.c_str(), value);
#else
        setenv(m_name.c_str(), value, 1);
#endif
    }

    void Unset()
    {
#if defined(_WIN32)
        _putenv_s(m_name.c_str(), "");
#else
        unsetenv(m_name.c_str());
#endif
    }

    std::string m_name;
    bool m_hadPrevious = false;
    std::string m_previous;
};

bool Expect(bool condition, const char* message)
{
    if (condition)
        return true;
    std::fprintf(stderr, "FAIL: %s\n", message);
    return false;
}

bool TestAdjustPopulationClampsAndLogs()
{
    xrSim::WorldState state;
    const xrSim::Handle region = state.CreateRegion("debug_region", 100);
    const xrSim::Handle species = state.CreateSpecies("blind_dog");

    xrSim::Result result = state.SetPopulation(region, species, 50);
    bool ok = Expect(result.ok, "SetPopulation accepts valid handles");

    result = state.ApplyAdjustPopulation(1, region, species, 500, 7);
    ok = Expect(result.ok, "positive adjust succeeds") && ok;
    ok = Expect(result.appliedDelta == 10, "positive adjust is rate-limited to 10 percent") && ok;
    ok = Expect(state.Population(region, species) == 60, "positive adjust updates population") && ok;
    ok = Expect(state.ToolLog().size() == 1, "positive adjust appends one log record") && ok;
    ok = Expect(state.ToolLog().back().accepted, "positive adjust log is accepted") && ok;
    ok = Expect(state.ToolLog().back().requestedDelta == 500, "positive adjust records requested delta") && ok;
    ok = Expect(state.ToolLog().back().appliedDelta == 10, "positive adjust records applied delta") && ok;

    result = state.ApplyAdjustPopulation(2, region, species, -500, 7);
    ok = Expect(result.ok, "negative adjust succeeds") && ok;
    ok = Expect(result.appliedDelta == -10, "negative adjust is rate-limited to 10 percent") && ok;
    ok = Expect(state.Population(region, species) == 50, "negative adjust updates population") && ok;
    ok = Expect(state.ToolLog().size() == 2, "negative adjust appends second log record") && ok;
    return ok;
}

bool TestMissingHandleFailsAsValue()
{
    xrSim::WorldState state;
    const xrSim::Handle region = state.CreateRegion("debug_region", 100);
    const xrSim::Handle species = state.CreateSpecies("blind_dog");

    xrSim::Result result = state.ApplyAdjustPopulation(1, region, xrSim::Handle::Invalid(), 5, 1);
    bool ok = Expect(!result.ok, "missing species fails as a value");
    ok = Expect(result.reason.find("species") != std::string::npos, "missing species explains reason") && ok;
    ok = Expect(state.Population(region, species) == 0, "missing species does not mutate population") && ok;
    ok = Expect(state.ToolLog().size() == 1, "missing species is still logged") && ok;
    ok = Expect(!state.ToolLog().back().accepted, "missing species log is rejected") && ok;
    return ok;
}

bool TestDigestIsStable()
{
    xrSim::WorldState state;
    const xrSim::Handle region = state.CreateRegion("debug_region", 100);
    const xrSim::Handle species = state.CreateSpecies("blind_dog");
    xrSim::Result result = state.SetPopulation(region, species, 12);
    bool ok = Expect(result.ok, "digest setup accepts valid population");

    ok = Expect(
        state.Digest() == "regions=1 species=1 cohorts=1 log=0 pop{debug_region:blind_dog=12}",
        "digest is stable and human-readable") && ok;
    return ok;
}

bool TestSnapshotRoundTripPreservesStateAndLog()
{
    xrSim::WorldState state;
    const xrSim::Handle region = state.CreateRegion("debug_region", 100);
    const xrSim::Handle species = state.CreateSpecies("blind_dog");
    xrSim::Result result = state.SetPopulation(region, species, 50);
    bool ok = Expect(result.ok, "snapshot setup accepts valid population");
    result = state.ApplyAdjustPopulation(1, region, species, 500, 7);
    ok = Expect(result.ok, "snapshot setup accepts adjustment") && ok;

    const std::string snapshot = state.SaveSnapshot();
    ok = Expect(snapshot.find("xrsim_snapshot_v1") == 0, "snapshot has stable version header") && ok;
    ok = Expect(snapshot.find("tool 1 7 adjust_population 16777216 16777216 1 500 10") != std::string::npos,
        "snapshot records tool log entries") && ok;

    xrSim::WorldState restored;
    result = restored.LoadSnapshot(snapshot);
    ok = Expect(result.ok, "snapshot load succeeds") && ok;
    ok = Expect(restored.Digest() == state.Digest(), "snapshot round trip preserves digest") && ok;
    ok = Expect(restored.ToolLog().size() == 1, "snapshot round trip preserves tool log size") && ok;
    ok = Expect(restored.ToolLog().back().appliedDelta == 10, "snapshot round trip preserves tool log delta") && ok;
    return ok;
}

bool TestSnapshotLoadRejectsBadVersionAsValue()
{
    xrSim::WorldState restored;
    const xrSim::Result result = restored.LoadSnapshot("xrsim_snapshot_v2\n");
    bool ok = Expect(!result.ok, "bad snapshot version fails as a value");
    ok = Expect(result.reason.find("version") != std::string::npos, "bad snapshot version explains reason") && ok;
    ok = Expect(restored.Digest() == "regions=0 species=0 cohorts=0 log=0 pop{}",
        "bad snapshot load does not mutate destination") && ok;
    return ok;
}

bool TestReplayToolLogReproducesRecordedDigest()
{
    xrSim::WorldState baseline;
    const xrSim::Handle region = baseline.CreateRegion("debug_region", 100);
    const xrSim::Handle species = baseline.CreateSpecies("blind_dog");
    xrSim::Result result = baseline.SetPopulation(region, species, 50);
    bool ok = Expect(result.ok, "replay baseline accepts valid population");

    const std::string baselineSnapshot = baseline.SaveSnapshot();

    xrSim::WorldState recorded;
    result = recorded.LoadSnapshot(baselineSnapshot);
    ok = Expect(result.ok, "replay recorded state loads baseline") && ok;
    result = recorded.ApplyAdjustPopulation(1, region, species, 500, 7);
    ok = Expect(result.ok, "replay recorded state accepts adjustment") && ok;

    xrSim::WorldState replayed;
    result = replayed.LoadSnapshot(baselineSnapshot);
    ok = Expect(result.ok, "replay target loads baseline") && ok;
    result = replayed.ReplayToolLogFrom(recorded);
    ok = Expect(result.ok, "replay applies recorded log") && ok;
    ok = Expect(replayed.Digest() == recorded.Digest(), "replay reproduces recorded digest") && ok;
    ok = Expect(replayed.ToolLog().size() == recorded.ToolLog().size(), "replay reproduces log size") && ok;
    ok = Expect(replayed.ToolLog().back().appliedDelta == recorded.ToolLog().back().appliedDelta,
        "replay reproduces applied delta") && ok;
    return ok;
}

bool TestReplayDetectsDivergentBaseline()
{
    xrSim::WorldState baseline;
    const xrSim::Handle region = baseline.CreateRegion("debug_region", 100);
    const xrSim::Handle species = baseline.CreateSpecies("blind_dog");
    xrSim::Result result = baseline.SetPopulation(region, species, 50);
    bool ok = Expect(result.ok, "divergent replay baseline accepts population");

    xrSim::WorldState recorded;
    result = recorded.LoadSnapshot(baseline.SaveSnapshot());
    ok = Expect(result.ok, "divergent replay recorded loads baseline") && ok;
    result = recorded.ApplyAdjustPopulation(1, region, species, 500, 7);
    ok = Expect(result.ok, "divergent replay recorded accepts adjustment") && ok;

    xrSim::WorldState divergent;
    result = divergent.LoadSnapshot(baseline.SaveSnapshot());
    ok = Expect(result.ok, "divergent replay target loads baseline") && ok;
    result = divergent.SetPopulation(region, species, 95);
    ok = Expect(result.ok, "divergent replay target mutates baseline") && ok;
    result = divergent.ReplayToolLogFrom(recorded);
    ok = Expect(!result.ok, "divergent replay fails as a value") && ok;
    ok = Expect(result.reason.find("replay mismatch") != std::string::npos, "divergent replay explains mismatch") && ok;
    return ok;
}

bool TestNullProviderReturnsDeterministicIntent()
{
    xrSim::NullAgentProvider provider;
    xrSim::AgentWakeContext context;
    context.agentId = 1;
    context.gameDay = 3;
    context.observation = "regions=1 species=1 cohorts=1 log=0 pop{debug_region:blind_dog=50}";

    const xrSim::AgentProviderResult result = provider.Wake(context);
    bool ok = Expect(result.ok, "null provider succeeds without network");
    ok = Expect(result.provider == "null", "null provider reports provider id") && ok;
    ok = Expect(result.model == "deterministic-null", "null provider reports deterministic model") && ok;
    ok = Expect(result.intents.size() == 1, "null provider emits one intent") && ok;
    if (result.intents.size() == 1)
    {
        ok = Expect(result.intents[0].tool == "adjust_population", "null provider emits adjust_population") && ok;
        ok = Expect(result.intents[0].region == "debug_region", "null provider targets debug region") && ok;
        ok = Expect(result.intents[0].species == "blind_dog", "null provider targets blind dog") && ok;
        ok = Expect(result.intents[0].delta == 5, "null provider emits deterministic delta") && ok;
    }
    return ok;
}

bool TestRuntimeUsesRecordedProviderIntent()
{
    xrSim::WorldState state;
    const xrSim::Handle region = state.CreateRegion("debug_region", 100);
    const xrSim::Handle species = state.CreateSpecies("blind_dog");
    xrSim::Result result = state.SetPopulation(region, species, 50);
    bool ok = Expect(result.ok, "recorded provider setup accepts population");

    xrSim::AgentProviderResult wake;
    wake.ok = true;
    wake.provider = "recorded";
    wake.model = "fixture";
    xrSim::AgentIntent intent;
    intent.tool = "adjust_population";
    intent.region = "debug_region";
    intent.species = "blind_dog";
    intent.delta = 7;
    wake.intents.push_back(intent);

    std::vector<xrSim::AgentProviderResult> script;
    script.push_back(wake);
    xrSim::RecordedAgentProvider provider(script);
    xrSim::NullAgentRuntime runtime;
    runtime.SetProvider(&provider);

    result = runtime.Wake(state, 9);
    ok = Expect(result.ok, "runtime accepts recorded provider intent") && ok;
    ok = Expect(result.appliedDelta == 7, "runtime applies recorded provider delta") && ok;
    ok = Expect(state.Population(region, species) == 57, "runtime mutates state from provider intent") && ok;
    ok = Expect(state.ToolLog().size() == 1, "runtime logs recorded provider intent") && ok;
    ok = Expect(state.ToolLog().back().gameDay == 9, "runtime records provider game day") && ok;
    ok = Expect(runtime.LastProviderResult().provider == "recorded", "runtime exposes last provider id") && ok;
    ok = Expect(runtime.LastProviderResult().model == "fixture", "runtime exposes last provider model") && ok;
    return ok;
}

bool TestRecordedProviderExhaustionFailsAsValue()
{
    std::vector<xrSim::AgentProviderResult> script;
    xrSim::RecordedAgentProvider provider(script);
    xrSim::AgentWakeContext context;
    context.agentId = 1;

    const xrSim::AgentProviderResult result = provider.Wake(context);
    bool ok = Expect(!result.ok, "recorded provider exhaustion fails as a value");
    ok = Expect(result.error.find("exhausted") != std::string::npos, "recorded provider explains exhaustion") && ok;
    return ok;
}

bool TestAgentPromptIncludesObservationAndToolSchema()
{
    xrSim::AgentWakeContext context;
    context.agentId = 7;
    context.gameDay = 11;
    context.observation = "regions=1 species=1 cohorts=1 log=0 pop{debug_region:blind_dog=50}";

    const std::string prompt = xrSim::BuildAgentWakePrompt(context);
    bool ok = Expect(prompt.find("xrsim_agent_wake_v1") == 0, "agent prompt has stable version header");
    ok = Expect(prompt.find("agent_id 7") != std::string::npos, "agent prompt records agent id") && ok;
    ok = Expect(prompt.find("game_day 11") != std::string::npos, "agent prompt records game day") && ok;
    ok = Expect(prompt.find(context.observation) != std::string::npos, "agent prompt includes observation") && ok;
    ok = Expect(prompt.find("intent adjust_population <region> <species> <delta:int>") != std::string::npos,
        "agent prompt includes tool schema") && ok;
    ok = Expect(prompt.find("coast") != std::string::npos, "agent prompt allows coast response") && ok;
    return ok;
}

bool TestAgentResponseParserAcceptsIntentLines()
{
    const char* response =
        "xrsim_agent_response_v1\n"
        "intent adjust_population debug_region blind_dog -4\n"
        "end\n";

    const xrSim::AgentProviderResult result =
        xrSim::ParseAgentProviderResponse(response, "anthropic", "claude-sonnet-5");
    bool ok = Expect(result.ok, "agent response parser accepts valid response");
    ok = Expect(result.provider == "anthropic", "agent response parser stamps provider") && ok;
    ok = Expect(result.model == "claude-sonnet-5", "agent response parser stamps model") && ok;
    ok = Expect(result.intents.size() == 1, "agent response parser emits one intent") && ok;
    if (result.intents.size() == 1)
    {
        ok = Expect(result.intents[0].tool == "adjust_population", "agent response parser preserves tool") && ok;
        ok = Expect(result.intents[0].region == "debug_region", "agent response parser preserves region") && ok;
        ok = Expect(result.intents[0].species == "blind_dog", "agent response parser preserves species") && ok;
        ok = Expect(result.intents[0].delta == -4, "agent response parser preserves delta") && ok;
    }
    return ok;
}

bool TestAgentResponseParserRejectsUnknownIntentAsValue()
{
    const char* response =
        "xrsim_agent_response_v1\n"
        "intent teleport debug_region blind_dog 4\n"
        "end\n";

    const xrSim::AgentProviderResult result =
        xrSim::ParseAgentProviderResponse(response, "anthropic", "claude-sonnet-5");
    bool ok = Expect(!result.ok, "agent response parser rejects unknown intent as a value");
    ok = Expect(result.error.find("unknown intent") != std::string::npos, "agent response parser explains unknown intent") && ok;
    return ok;
}

bool TestAgentResponseParserAcceptsCoastAsNormalValue()
{
    const char* response =
        "xrsim_agent_response_v1\n"
        "coast\n"
        "end\n";

    const xrSim::AgentProviderResult result =
        xrSim::ParseAgentProviderResponse(response, "anthropic", "claude-sonnet-5");
    bool ok = Expect(result.ok, "agent response parser accepts coast as normal value");
    ok = Expect(result.coast, "agent response parser marks coast") && ok;
    ok = Expect(result.intents.empty(), "agent response coast emits no intents") && ok;
    return ok;
}

bool TestAgentResponseParserAcceptsEchoedMetadataBeforeCoast()
{
    const char* response =
        "xrsim_agent_response_v1\n"
        "agent_id 1\n"
        "game_day 0\n"
        "coast\n"
        "end\n";

    const xrSim::AgentProviderResult result =
        xrSim::ParseAgentProviderResponse(response, "anthropic", "claude-sonnet-5");
    bool ok = Expect(result.ok, "agent response parser accepts echoed metadata before coast");
    ok = Expect(result.coast, "agent response parser keeps coast with echoed metadata") && ok;
    ok = Expect(result.intents.empty(), "agent response echoed coast emits no intents") && ok;
    return ok;
}

bool TestAgentResponseParserAcceptsActionCoastWithReason()
{
    const char* response =
        "xrsim_agent_response_v1\n"
        "agent_id 1\n"
        "game_day 0\n"
        "action coast\n"
        "reason single_observation_no_trend_data population=50 stable_debug_region no_intervention_warranted\n"
        "end\n";

    const xrSim::AgentProviderResult result =
        xrSim::ParseAgentProviderResponse(response, "anthropic", "claude-sonnet-5");
    bool ok = Expect(result.ok, "agent response parser accepts action coast");
    ok = Expect(result.coast, "agent response parser marks action coast") && ok;
    ok = Expect(
        result.coastReason == "single_observation_no_trend_data population=50 stable_debug_region no_intervention_warranted",
        "agent response parser preserves action coast reason") && ok;
    ok = Expect(result.intents.empty(), "agent response action coast emits no intents") && ok;
    return ok;
}

bool TestAgentResponseParserFindsVersionAfterModelPreface()
{
    const char* response =
        "Here is the response:\n"
        "```text\n"
        "xrsim_agent_response_v1\n"
        "coast\n"
        "end\n"
        "```\n";

    const xrSim::AgentProviderResult result =
        xrSim::ParseAgentProviderResponse(response, "anthropic", "claude-sonnet-5");
    bool ok = Expect(result.ok, "agent response parser finds version after model preface");
    ok = Expect(result.coast, "agent response parser keeps coast inside fenced response") && ok;
    return ok;
}

bool TestActuatorConvertsSquadIntentToCommands()
{
    xrSim::WorldState state;
    state.CreateRegion("debug_region", 100);
    state.CreateSpecies("blind_dog");

    xrSim::ActorIntentPlan plan;
    plan.goal = "survive_and_delay_player";
    plan.stance = "cautious";
    plan.durationMs = 3500;
    plan.actions.push_back(xrSim::ActorAction{ "move_to", "cover_node_12", "", "", 0 });
    plan.actions.push_back(xrSim::ActorAction{ "fire_pattern", "target_player", "burst_short", "", 0 });

    const xrSim::ActuatorResult result =
        xrSim::ExecuteActorIntent(state, xrSim::MakeDebugSquadAgent(), plan, 2);

    bool ok = Expect(result.ok, "actuator accepts squad intent");
    ok = Expect(result.commands.size() == 3, "actuator emits stance plus two commands") && ok;
    if (result.commands.size() == 3)
    {
        ok = Expect(result.commands[0].verb == "set_stance", "actuator emits stance command") && ok;
        ok = Expect(result.commands[1].verb == "move_to", "actuator emits move command") && ok;
        ok = Expect(result.commands[2].arg0 == "burst_short", "actuator keeps fire pattern argument") && ok;
        ok = Expect(
            xrSim::FormatActuatorCommands(result.commands) ==
                "set_stance cautious | move_to cover_node_12 | fire_pattern target_player burst_short",
            "actuator formats commands in stable order") && ok;
    }
    return ok;
}

bool TestActuatorRejectsIllegalPackVerbAsValue()
{
    xrSim::WorldState state;
    xrSim::ActorIntentPlan plan;
    plan.actions.push_back(xrSim::ActorAction{ "fire_pattern", "target_player", "burst_short", "", 0 });

    const xrSim::ActuatorResult result =
        xrSim::ExecuteActorIntent(state, xrSim::MakeDebugMutantPackAgent(), plan, 0);

    bool ok = Expect(!result.ok, "actuator rejects illegal pack verb as value");
    ok = Expect(result.reason.find("illegal action") != std::string::npos, "actuator explains illegal pack verb") && ok;
    return ok;
}

bool TestActuatorAppliesPopulationToolThroughWorldState()
{
    xrSim::WorldState state;
    const xrSim::Handle region = state.CreateRegion("debug_region", 100);
    const xrSim::Handle species = state.CreateSpecies("blind_dog");
    xrSim::Result setup = state.SetPopulation(region, species, 50);
    bool ok = Expect(setup.ok, "actuator population setup accepts population");

    xrSim::ActorIntentPlan plan;
    plan.actions.push_back(xrSim::ActorAction{ "adjust_population", "debug_region", "blind_dog", "", 500 });

    const xrSim::ActuatorResult result =
        xrSim::ExecuteActorIntent(state, xrSim::MakeDebugMutantPackAgent(), plan, 4);

    ok = Expect(result.ok, "actuator accepts population tool") && ok;
    ok = Expect(result.appliedDelta == 10, "actuator applies clamped population delta") && ok;
    ok = Expect(state.Population(region, species) == 60, "actuator mutates world state through validator") && ok;
    return ok;
}

bool TestActuatorRejectsMixedPlanAtomically()
{
    xrSim::WorldState state;
    const xrSim::Handle region = state.CreateRegion("debug_region", 100);
    const xrSim::Handle species = state.CreateSpecies("blind_dog");
    xrSim::Result setup = state.SetPopulation(region, species, 50);
    bool ok = Expect(setup.ok, "mixed actuator setup accepts population");

    xrSim::ActorIntentPlan plan;
    plan.actions.push_back(xrSim::ActorAction{ "adjust_population", "debug_region", "blind_dog", "", 500 });
    plan.actions.push_back(xrSim::ActorAction{ "fire_pattern", "target_player", "burst_short", "", 0 });

    const xrSim::ActuatorResult result =
        xrSim::ExecuteActorIntent(state, xrSim::MakeDebugMutantPackAgent(), plan, 4);

    ok = Expect(!result.ok, "actuator rejects mixed plan with illegal tail action") && ok;
    ok = Expect(result.reason.find("illegal action") != std::string::npos,
        "actuator mixed-plan failure explains illegal action") && ok;
    ok = Expect(state.Population(region, species) == 50, "actuator mixed-plan failure leaves population unchanged") && ok;
    ok = Expect(state.ToolLog().empty(), "actuator mixed-plan failure leaves tool log unchanged") && ok;
    ok = Expect(result.commands.empty(), "actuator mixed-plan failure returns no partial command stream") && ok;
    return ok;
}

bool TestRecordedTextProviderParsesScriptedWake()
{
    std::vector<std::string> script;
    script.push_back(
        "xrsim_agent_response_v1\n"
        "intent adjust_population debug_region blind_dog 6\n"
        "end\n");

    xrSim::RecordedTextAgentProvider provider("recorded-text", "fixture", script);
    xrSim::AgentWakeContext context;
    context.agentId = 1;

    const xrSim::AgentProviderResult result = provider.Wake(context);
    bool ok = Expect(result.ok, "recorded text provider parses scripted response");
    ok = Expect(result.provider == "recorded-text", "recorded text provider stamps provider") && ok;
    ok = Expect(result.model == "fixture", "recorded text provider stamps model") && ok;
    ok = Expect(result.intents.size() == 1, "recorded text provider emits scripted intent") && ok;
    if (result.intents.size() == 1)
        ok = Expect(result.intents[0].delta == 6, "recorded text provider preserves scripted delta") && ok;
    return ok;
}

bool TestRuntimeCoastsOnProviderCoastResult()
{
    xrSim::WorldState state;
    const xrSim::Handle region = state.CreateRegion("debug_region", 100);
    const xrSim::Handle species = state.CreateSpecies("blind_dog");
    xrSim::Result result = state.SetPopulation(region, species, 50);
    bool ok = Expect(result.ok, "coast provider setup accepts population");

    xrSim::AgentProviderResult wake;
    wake.ok = true;
    wake.provider = "recorded";
    wake.model = "fixture";
    wake.coast = true;

    std::vector<xrSim::AgentProviderResult> script;
    script.push_back(wake);
    xrSim::RecordedAgentProvider provider(script);
    xrSim::NullAgentRuntime runtime;
    runtime.SetProvider(&provider);

    result = runtime.Wake(state, 12);
    ok = Expect(result.ok, "runtime accepts provider coast") && ok;
    ok = Expect(result.appliedDelta == 0, "runtime coast applies no delta") && ok;
    ok = Expect(result.reason == "coast", "runtime coast reports coast reason") && ok;
    ok = Expect(state.Population(region, species) == 50, "runtime coast leaves population unchanged") && ok;
    ok = Expect(state.ToolLog().empty(), "runtime coast records no tool call") && ok;
    ok = Expect(runtime.WakeCount() == 1, "runtime coast counts as completed wake") && ok;
    return ok;
}

bool TestAgentProviderConfigDefaultsToSonnetTier()
{
    std::vector<xrSim::AgentConfigVar> vars;
    const xrSim::AgentProviderConfig config = xrSim::BuildAgentProviderConfig(vars);

    bool ok = Expect(config.enabled, "agent provider config defaults enabled for live shell");
    ok = Expect(config.provider == "anthropic", "agent provider config defaults to anthropic") && ok;
    ok = Expect(config.model == "claude-sonnet-5", "agent provider config defaults to sonnet tier") && ok;
    ok = Expect(config.apiKey.empty(), "agent provider config does not invent api key") && ok;
    ok = Expect(config.timeoutMs == 30000, "agent provider config uses bounded default timeout") && ok;
    return ok;
}

bool TestAgentProviderConfigReadsEnvironmentValues()
{
    std::vector<xrSim::AgentConfigVar> vars;
    vars.push_back(xrSim::AgentConfigVar{ "XRAY_AGENT_PROVIDER", "anthropic" });
    vars.push_back(xrSim::AgentConfigVar{ "XRAY_AGENT_MODEL", "claude-sonnet-5-test" });
    vars.push_back(xrSim::AgentConfigVar{ "XRAY_AGENT_API_KEY", "secret" });
    vars.push_back(xrSim::AgentConfigVar{ "XRAY_AGENT_TIMEOUT_MS", "1200" });

    const xrSim::AgentProviderConfig config = xrSim::BuildAgentProviderConfig(vars);
    bool ok = Expect(config.provider == "anthropic", "agent provider config reads provider");
    ok = Expect(config.model == "claude-sonnet-5-test", "agent provider config reads model") && ok;
    ok = Expect(config.apiKey == "secret", "agent provider config reads api key") && ok;
    ok = Expect(config.timeoutMs == 1200, "agent provider config reads timeout") && ok;
    return ok;
}

bool TestAnthropicProviderShellCoastsWithoutApiKey()
{
    xrSim::AgentProviderConfig config;
    config.provider = "anthropic";
    config.model = "claude-sonnet-5";
    config.enabled = true;
    xrSim::AnthropicAgentProviderShell provider(config);

    xrSim::AgentWakeContext context;
    context.agentId = 1;
    context.gameDay = 2;
    context.observation = "regions=1 species=1 cohorts=1 log=0 pop{debug_region:blind_dog=50}";

    const xrSim::AgentProviderResult result = provider.Wake(context);
    bool ok = Expect(result.ok, "anthropic shell missing key succeeds as coast");
    ok = Expect(result.coast, "anthropic shell missing key marks coast") && ok;
    ok = Expect(result.provider == "anthropic", "anthropic shell stamps provider") && ok;
    ok = Expect(result.model == "claude-sonnet-5", "anthropic shell stamps model") && ok;
    ok = Expect(result.coastReason == "missing_api_key", "anthropic shell explains missing key coast") && ok;
    return ok;
}

bool TestAgentProviderLedgerFormatsReplayMetadata()
{
    xrSim::AgentWakeContext context;
    context.agentId = 7;
    context.gameDay = 11;

    xrSim::AgentProviderResult result;
    result.ok = true;
    result.coast = true;
    result.provider = "anthropic";
    result.model = "claude-sonnet-5";
    result.coastReason = "missing_api_key";

    const std::string ledger = xrSim::FormatAgentProviderLedgerRecord(4, context, result, 123);
    bool ok = Expect(ledger.find("xrsim_agent_provider_record_v1") == 0, "agent provider ledger has stable header");
    ok = Expect(ledger.find("seq=4") != std::string::npos, "agent provider ledger records seq") && ok;
    ok = Expect(ledger.find("agent_id=7") != std::string::npos, "agent provider ledger records agent id") && ok;
    ok = Expect(ledger.find("game_day=11") != std::string::npos, "agent provider ledger records game day") && ok;
    ok = Expect(ledger.find("provider=anthropic") != std::string::npos, "agent provider ledger records provider") && ok;
    ok = Expect(ledger.find("model=claude-sonnet-5") != std::string::npos, "agent provider ledger records model") && ok;
    ok = Expect(ledger.find("coast=1") != std::string::npos, "agent provider ledger records coast") && ok;
    ok = Expect(ledger.find("coast_reason=missing_api_key") != std::string::npos,
        "agent provider ledger records coast reason") && ok;
    ok = Expect(ledger.find("latency_ms=123") != std::string::npos, "agent provider ledger records latency") && ok;
    return ok;
}

bool TestAnthropicRequestEnvelopeUsesMessagesApiShape()
{
    xrSim::AgentProviderConfig config;
    config.provider = "anthropic";
    config.model = "claude-sonnet-5";

    xrSim::AgentWakeContext context;
    context.agentId = 9;
    context.gameDay = 13;
    context.observation = "quote=\"zone\"\nslash=\\";

    const xrSim::AnthropicMessagesRequest request = xrSim::BuildAnthropicMessagesRequest(config, context, 768);
    bool ok = Expect(request.method == "POST", "anthropic request uses POST");
    ok = Expect(request.path == "/v1/messages", "anthropic request targets messages api") && ok;
    ok = Expect(request.anthropicVersion == "2023-06-01", "anthropic request pins api version") && ok;
    ok = Expect(request.contentType == "application/json", "anthropic request uses json content type") && ok;
    ok = Expect(request.body.find("\"model\":\"claude-sonnet-5\"") != std::string::npos,
        "anthropic request includes model") && ok;
    ok = Expect(request.body.find("\"max_tokens\":768") != std::string::npos,
        "anthropic request includes max tokens") && ok;
    ok = Expect(request.body.find("\"messages\":[{\"role\":\"user\",\"content\":\"") != std::string::npos,
        "anthropic request includes user message") && ok;
    ok = Expect(request.body.find("quote=\\\"zone\\\"") != std::string::npos,
        "anthropic request escapes quotes") && ok;
    ok = Expect(request.body.find("slash=\\\\") != std::string::npos,
        "anthropic request escapes backslash") && ok;
    return ok;
}

bool TestAnthropicTextResponseParsesThroughAgentCodec()
{
    const char* response =
        "{\"id\":\"msg_test\",\"type\":\"message\",\"role\":\"assistant\","
        "\"content\":[{\"type\":\"text\",\"text\":\"xrsim_agent_response_v1\\ncoast\\nend\\n\"}],"
        "\"model\":\"claude-sonnet-5\",\"stop_reason\":\"end_turn\"}";

    const xrSim::AgentProviderResult result =
        xrSim::ParseAnthropicMessagesTextResponse(response, "anthropic", "claude-sonnet-5");
    bool ok = Expect(result.ok, "anthropic text response parses through agent codec");
    ok = Expect(result.coast, "anthropic text response preserves coast") && ok;
    ok = Expect(result.provider == "anthropic", "anthropic text response stamps provider") && ok;
    ok = Expect(result.model == "claude-sonnet-5", "anthropic text response stamps model") && ok;
    return ok;
}

bool TestAnthropicProviderShellUsesInjectedTransport()
{
    xrSim::AgentProviderConfig config;
    config.provider = "anthropic";
    config.model = "claude-sonnet-5";
    config.apiKey = "secret";

    FakeAnthropicTransport transport;
    transport.result.ok = true;
    transport.result.status = 200;
    transport.result.latencyMs = 44;
    transport.result.body =
        "{\"content\":[{\"type\":\"text\",\"text\":\"xrsim_agent_response_v1\\n"
        "intent adjust_population debug_region blind_dog 3\\nend\\n\"}]}";

    xrSim::AnthropicAgentProviderShell provider(config);
    provider.SetTransport(&transport);

    xrSim::AgentWakeContext context;
    context.agentId = 1;
    context.gameDay = 2;
    context.observation = "regions=1 species=1 cohorts=1 log=0 pop{debug_region:blind_dog=50}";

    const xrSim::AgentProviderResult result = provider.Wake(context);
    bool ok = Expect(result.ok, "anthropic shell accepts injected transport response");
    ok = Expect(!result.coast, "anthropic shell transport response is not coast") && ok;
    ok = Expect(result.intents.size() == 1, "anthropic shell transport emits one intent") && ok;
    if (result.intents.size() == 1)
        ok = Expect(result.intents[0].delta == 3, "anthropic shell transport preserves delta") && ok;
    ok = Expect(transport.calls == 1, "anthropic shell calls injected transport once") && ok;
    ok = Expect(transport.seenModel == "claude-sonnet-5", "anthropic shell transport sees model") && ok;
    ok = Expect(transport.seenPath == "/v1/messages", "anthropic shell transport sees messages path") && ok;
    return ok;
}

bool TestAnthropicProviderShellCoastsOnTransportFailure()
{
    xrSim::AgentProviderConfig config;
    config.provider = "anthropic";
    config.model = "claude-sonnet-5";
    config.apiKey = "secret";

    FakeAnthropicTransport transport;
    transport.result.ok = false;
    transport.result.error = "timeout";
    transport.result.latencyMs = 600;

    xrSim::AnthropicAgentProviderShell provider(config);
    provider.SetTransport(&transport);

    xrSim::AgentWakeContext context;
    context.agentId = 1;

    const xrSim::AgentProviderResult result = provider.Wake(context);
    bool ok = Expect(result.ok, "anthropic shell transport failure succeeds as coast");
    ok = Expect(result.coast, "anthropic shell transport failure marks coast") && ok;
    ok = Expect(result.coastReason == "transport_error", "anthropic shell transport failure records coast reason") && ok;
    ok = Expect(result.error == "timeout", "anthropic shell transport failure keeps error detail") && ok;
    return ok;
}

bool TestAnthropicActorProviderParsesInjectedTransportIntent()
{
    xrSim::AgentProviderConfig config;
    config.provider = "anthropic";
    config.model = "claude-sonnet-5";
    config.apiKey = "secret";

    FakeAnthropicTransport transport;
    transport.result.ok = true;
    transport.result.status = 200;
    transport.result.body =
        "{\"content\":[{\"type\":\"text\",\"text\":\"xrsim_actor_intent_v1\\n"
        "goal avoid_player_until_dark\\n"
        "stance cautious\\n"
        "duration_ms 4000\\n"
        "action stalk target_player crescent\\n"
        "end\\n\"}]}";

    xrSim::AnthropicActorIntentProvider provider(config);
    provider.SetTransport(&transport);

    xrSim::ActorWakeContext context;
    context.actor = xrSim::MakeDebugMutantPackAgent();
    context.observation = "agent_id 201\nscope MUTANT_PACK\n";
    context.prompt = "xrsim_actor_wake_v1\nagent_id 201\nreturn xrsim_actor_intent_v1\nend\n";

    const xrSim::ActorProviderResult result = provider.Wake(context);
    bool ok = Expect(result.ok, "anthropic actor provider accepts injected transport response");
    ok = Expect(!result.coast, "anthropic actor provider response is not coast") && ok;
    ok = Expect(result.provider == "anthropic", "anthropic actor provider stamps provider") && ok;
    ok = Expect(result.model == "claude-sonnet-5", "anthropic actor provider stamps model") && ok;
    ok = Expect(result.plan.goal == "avoid_player_until_dark", "anthropic actor provider parses goal") && ok;
    ok = Expect(result.plan.actions.size() == 1, "anthropic actor provider parses one action") && ok;
    if (result.plan.actions.size() == 1)
        ok = Expect(result.plan.actions[0].verb == "stalk", "anthropic actor provider parses stalk action") && ok;
    ok = Expect(transport.calls == 1, "anthropic actor provider calls injected transport once") && ok;
    ok = Expect(transport.seenPath == "/v1/messages", "anthropic actor provider uses messages api") && ok;
    ok = Expect(transport.seenBody.find("xrsim_actor_wake_v1") != std::string::npos,
        "anthropic actor request contains actor prompt") && ok;
    return ok;
}

bool TestAnthropicActorProviderCoastsWithoutApiKey()
{
    xrSim::AgentProviderConfig config;
    config.provider = "anthropic";
    config.model = "claude-sonnet-5";

    FakeAnthropicTransport transport;
    xrSim::AnthropicActorIntentProvider provider(config);
    provider.SetTransport(&transport);

    xrSim::ActorWakeContext context;
    context.actor = xrSim::MakeDebugSquadAgent();
    context.prompt = "xrsim_actor_wake_v1\nend\n";

    const xrSim::ActorProviderResult result = provider.Wake(context);
    bool ok = Expect(result.ok, "anthropic actor missing key succeeds as coast");
    ok = Expect(result.coast, "anthropic actor missing key marks coast") && ok;
    ok = Expect(result.coastReason == "missing_api_key", "anthropic actor missing key explains coast") && ok;
    ok = Expect(transport.calls == 0, "anthropic actor missing key skips transport") && ok;
    return ok;
}

bool TestAnthropicActorProviderCoastsOnTransportFailure()
{
    xrSim::AgentProviderConfig config;
    config.provider = "anthropic";
    config.model = "claude-sonnet-5";
    config.apiKey = "secret";

    FakeAnthropicTransport transport;
    transport.result.ok = false;
    transport.result.error = "timeout";

    xrSim::AnthropicActorIntentProvider provider(config);
    provider.SetTransport(&transport);

    xrSim::ActorWakeContext context;
    context.actor = xrSim::MakeDebugSquadAgent();
    context.prompt = "xrsim_actor_wake_v1\nend\n";

    const xrSim::ActorProviderResult result = provider.Wake(context);
    bool ok = Expect(result.ok, "anthropic actor transport failure succeeds as coast");
    ok = Expect(result.coast, "anthropic actor transport failure marks coast") && ok;
    ok = Expect(result.coastReason == "transport_error", "anthropic actor transport failure explains coast") && ok;
    ok = Expect(result.error == "timeout", "anthropic actor transport failure preserves detail") && ok;
    return ok;
}

bool TestAsyncActorProviderQueueRunsWakeOffCallerThread()
{
    auto provider = std::make_unique<BlockingActorProvider>();
    BlockingActorProvider* providerView = provider.get();
    xrSim::AsyncActorProviderQueue queue(std::move(provider));

    xrSim::ActorWakeContext context;
    context.actor = xrSim::MakeDebugMutantPackAgent();
    context.prompt = "xrsim_actor_wake_v1\nend\n";

    const xrSim::ActorWakeSubmitResult submitted = queue.Submit(context);
    bool ok = Expect(submitted.ok, "async actor queue accepts copied wake context");
    ok = Expect(submitted.requestId == 1, "async actor queue starts request ids at one") && ok;
    ok = Expect(providerView->WaitUntilEntered(), "async actor worker enters provider") && ok;

    xrSim::ActorWakePollResult polled = queue.Poll(submitted.requestId);
    ok = Expect(polled.ok, "async actor pending poll succeeds") && ok;
    ok = Expect(!polled.ready, "async actor first poll reports pending") && ok;
    ok = Expect(providerView->seenAgentId == context.actor.agentId, "async actor worker receives copied actor id") && ok;

    providerView->Release();
    for (uint32_t attempt = 0; attempt < 100; ++attempt)
    {
        polled = queue.Poll(submitted.requestId);
        if (polled.ready || !polled.ok)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    ok = Expect(polled.ok, "async actor completed poll succeeds") && ok;
    ok = Expect(polled.ready, "async actor completed poll becomes ready") && ok;
    ok = Expect(polled.providerResult.plan.goal == "queued_pack_goal", "async actor poll returns provider plan") && ok;

    const xrSim::ActorWakePollResult consumed = queue.Poll(submitted.requestId);
    ok = Expect(!consumed.ok, "async actor completion is consumed exactly once") && ok;
    ok = Expect(consumed.reason == "unknown_request", "async actor consumed request reports stable reason") && ok;

    const xrSim::ActorWakePollResult unknown = queue.Poll(999);
    ok = Expect(!unknown.ok, "async actor unknown request fails as value") && ok;
    ok = Expect(unknown.reason == "unknown_request", "async actor unknown request explains reason") && ok;
    return ok;
}

bool TestAsyncActorProviderQueueRejectsSubmitAfterShutdown()
{
    auto provider = std::make_unique<BlockingActorProvider>();
    xrSim::AsyncActorProviderQueue queue(std::move(provider));
    queue.Shutdown();

    xrSim::ActorWakeContext context;
    context.actor = xrSim::MakeDebugSquadAgent();
    const xrSim::ActorWakeSubmitResult submitted = queue.Submit(context);
    bool ok = Expect(!submitted.ok, "async actor queue rejects submit after shutdown");
    ok = Expect(submitted.reason == "queue_shutdown", "async actor shutdown submit explains reason") && ok;
    return ok;
}

bool TestAnthropicHttpTransportFactoryMatchesAvailability()
{
    const bool available = xrSim::IsAnthropicHttpTransportAvailable();
    const std::unique_ptr<xrSim::IAnthropicTransport> transport = xrSim::CreateAnthropicHttpTransport();

    bool ok = Expect((transport != nullptr) == available, "anthropic transport factory matches availability");
    const std::string description = xrSim::DescribeAnthropicHttpTransport();
    ok = Expect(description == "curl" || description == "unavailable",
        "anthropic transport description is stable") && ok;
    if (available)
        ok = Expect(description == "curl", "available anthropic transport reports curl") && ok;
    else
        ok = Expect(description == "unavailable", "unavailable anthropic transport reports unavailable") && ok;
    return ok;
}

bool TestActorIntentParserAcceptsSquadPlan()
{
    const char* text =
        "xrsim_actor_intent_v1\n"
        "goal survive_and_delay_player\n"
        "stance cautious\n"
        "duration_ms 3500\n"
        "action move_to cover_node_12\n"
        "action fire_pattern target_player burst_short\n"
        "action vocalize ally_3 fall_back\n"
        "memory player_used_grenade_aggressively\n"
        "end\n";

    const xrSim::ActorIntentParseResult parsed = xrSim::ParseActorIntentPlan(text);
    bool ok = Expect(parsed.ok, "actor intent parser accepts valid squad plan");
    ok = Expect(parsed.plan.goal == "survive_and_delay_player", "actor intent parser keeps goal") && ok;
    ok = Expect(parsed.plan.stance == "cautious", "actor intent parser keeps stance") && ok;
    ok = Expect(parsed.plan.durationMs == 3500, "actor intent parser keeps duration") && ok;
    ok = Expect(parsed.plan.actions.size() == 3, "actor intent parser keeps actions") && ok;
    if (parsed.plan.actions.size() == 3)
    {
        ok = Expect(parsed.plan.actions[0].verb == "move_to", "actor intent parser keeps first action verb") && ok;
        ok = Expect(parsed.plan.actions[0].target == "cover_node_12", "actor intent parser keeps first action target") && ok;
        ok = Expect(parsed.plan.actions[1].arg0 == "burst_short", "actor intent parser keeps action argument") && ok;
    }
    ok = Expect(parsed.plan.memories.size() == 1, "actor intent parser keeps memory records") && ok;
    return ok;
}

bool TestActorIntentParserAcceptsCoast()
{
    const xrSim::ActorIntentParseResult parsed = xrSim::ParseActorIntentPlan(
        "xrsim_actor_intent_v1\n"
        "coast\n"
        "end\n");

    bool ok = Expect(parsed.ok, "actor intent parser accepts coast");
    ok = Expect(parsed.plan.coast, "actor intent parser marks coast") && ok;
    ok = Expect(parsed.plan.actions.empty(), "actor intent coast has no actions") && ok;
    return ok;
}

bool TestActorIntentParserSupportsVerbOnlyActionFormatRoundTrip()
{
    const xrSim::ActorIntentParseResult parsed = xrSim::ParseActorIntentPlan(
        "xrsim_actor_intent_v1\n"
        "action wait\n"
        "end\n");
    bool ok = Expect(parsed.ok, "actor intent parser accepts verb-only action");
    ok = Expect(parsed.plan.actions.size() == 1, "actor intent verb-only action keeps action count") && ok;
    if (parsed.plan.actions.size() == 1)
    {
        ok = Expect(parsed.plan.actions[0].verb == "wait", "actor intent verb-only action keeps verb") && ok;
        ok = Expect(parsed.plan.actions[0].target.empty(), "actor intent verb-only action keeps empty target") && ok;
        ok = Expect(parsed.plan.actions[0].arg0.empty(), "actor intent verb-only action keeps empty arg0") && ok;
        ok = Expect(parsed.plan.actions[0].arg1.empty(), "actor intent verb-only action keeps empty arg1") && ok;
        ok = Expect(parsed.plan.actions[0].amount == 0, "actor intent verb-only action keeps zero amount") && ok;
    }

    xrSim::ActorIntentPlan plan;
    plan.actions.push_back(xrSim::ActorAction{ "wait", "", "", "", 0 });
    const std::string text = xrSim::FormatActorIntentPlan(plan);
    const xrSim::ActorIntentParseResult roundTripped = xrSim::ParseActorIntentPlan(text);
    ok = Expect(roundTripped.ok, "actor intent verb-only action round-trips through formatter") && ok;
    if (roundTripped.ok)
    {
        ok = Expect(roundTripped.plan.actions.size() == 1, "actor intent verb-only action format round-trip keeps action count") && ok;
        if (roundTripped.plan.actions.size() == 1)
        {
            ok = Expect(roundTripped.plan.actions[0].verb == "wait", "actor intent verb-only action format round-trip keeps verb") && ok;
            ok = Expect(roundTripped.plan.actions[0].target.empty(), "actor intent verb-only action format round-trip keeps empty target") && ok;
            ok = Expect(roundTripped.plan.actions[0].arg0.empty(), "actor intent verb-only action format round-trip keeps empty arg0") && ok;
            ok = Expect(roundTripped.plan.actions[0].arg1.empty(), "actor intent verb-only action format round-trip keeps empty arg1") && ok;
            ok = Expect(roundTripped.plan.actions[0].amount == 0, "actor intent verb-only action format round-trip keeps zero amount") && ok;
        }
    }
    return ok;
}

bool TestActorIntentParserRoutesAdjustPopulationAmountIntoActuator()
{
    xrSim::WorldState state;
    const xrSim::Handle region = state.CreateRegion("debug_region", 100);
    const xrSim::Handle species = state.CreateSpecies("blind_dog");
    xrSim::Result setup = state.SetPopulation(region, species, 50);
    bool ok = Expect(setup.ok, "actor intent adjust_population setup accepts population");

    const xrSim::ActorIntentParseResult parsed = xrSim::ParseActorIntentPlan(
        "xrsim_actor_intent_v1\n"
        "goal tune_spawn_budget\n"
        "action adjust_population debug_region blind_dog 500\n"
        "end\n");
    ok = Expect(parsed.ok, "actor intent parser accepts adjust_population amount form") && ok;
    ok = Expect(parsed.plan.actions.size() == 1, "actor intent adjust_population keeps single action") && ok;
    if (parsed.plan.actions.size() == 1)
    {
        ok = Expect(parsed.plan.actions[0].target == "debug_region",
            "actor intent adjust_population keeps region target") && ok;
        ok = Expect(parsed.plan.actions[0].arg0 == "blind_dog",
            "actor intent adjust_population keeps species in arg0") && ok;
        ok = Expect(parsed.plan.actions[0].arg1.empty(),
            "actor intent adjust_population does not treat amount as arg1") && ok;
        ok = Expect(parsed.plan.actions[0].amount == 500,
            "actor intent adjust_population routes parsed amount into amount field") && ok;
    }

    xrSim::ActorAgentRecord actor = xrSim::MakeDebugSquadAgent();
    const xrSim::ActuatorResult executed = xrSim::ExecuteActorIntent(state, actor, parsed.plan, 9);
    ok = Expect(executed.ok, "actor intent adjust_population executes parsed plan") && ok;
    ok = Expect(executed.appliedDelta == 10, "actor intent adjust_population applies parsed amount through clamp") && ok;
    ok = Expect(state.Population(region, species) == 60,
        "actor intent adjust_population changes world population by parsed amount") && ok;
    return ok;
}

bool TestActorIntentParserRejectsMalformedAdjustPopulationAction()
{
    const xrSim::ActorIntentParseResult missingAmount = xrSim::ParseActorIntentPlan(
        "xrsim_actor_intent_v1\n"
        "action adjust_population debug_region blind_dog\n"
        "end\n");
    bool ok = Expect(!missingAmount.ok, "actor intent parser rejects adjust_population without amount");

    const xrSim::ActorIntentParseResult extraToken = xrSim::ParseActorIntentPlan(
        "xrsim_actor_intent_v1\n"
        "action adjust_population debug_region blind_dog 5 extra\n"
        "end\n");
    ok = Expect(!extraToken.ok, "actor intent parser rejects adjust_population extra token") && ok;
    return ok;
}

bool TestActorIntentParserRejectsTrailingTokens()
{
    struct Case
    {
        const char* text;
        const char* message;
    };

    const Case cases[] = {
        {"xrsim_actor_intent_v1\ngoal survive_and_delay_player extra\nend\n", "actor intent parser rejects extra goal token"},
        {"xrsim_actor_intent_v1\nstance cautious extra\nend\n", "actor intent parser rejects extra stance token"},
        {"xrsim_actor_intent_v1\nduration_ms 3500 extra\nend\n", "actor intent parser rejects extra duration token"},
        {"xrsim_actor_intent_v1\nmemory player_used_grenade_aggressively extra\nend\n", "actor intent parser rejects extra memory token"},
        {
            "xrsim_actor_intent_v1\naction move_to cover_node_12 burst_short 7 extra\nend\n",
            "actor intent parser rejects extra action token",
        },
        {
            "xrsim_actor_intent_v1\naction move_to cover_node_12 burst_short arg bad_amount\nend\n",
            "actor intent parser rejects non-int action amount",
        },
    };

    bool ok = true;
    for (const Case& currentCase : cases)
    {
        const xrSim::ActorIntentParseResult parsed = xrSim::ParseActorIntentPlan(currentCase.text);
        ok = Expect(!parsed.ok, currentCase.message) && ok;
    }

    return ok;
}

bool TestActorIntentParserRejectsMixedCoastAndMetadataOrActions()
{
    const xrSim::ActorIntentParseResult mixedWithGoal =
        xrSim::ParseActorIntentPlan("xrsim_actor_intent_v1\ngoal survive_and_delay_player\ncoast\nend\n");
    bool ok = Expect(!mixedWithGoal.ok, "actor intent parser rejects goal mixed with coast");

    const xrSim::ActorIntentParseResult mixedWithStance =
        xrSim::ParseActorIntentPlan("xrsim_actor_intent_v1\ncoast\nstance cautious\nend\n");
    ok = Expect(!mixedWithStance.ok, "actor intent parser rejects coast mixed with stance") && ok;

    const xrSim::ActorIntentParseResult mixedWithDuration =
        xrSim::ParseActorIntentPlan("xrsim_actor_intent_v1\ncoast\nduration_ms 2500\nend\n");
    ok = Expect(!mixedWithDuration.ok, "actor intent parser rejects coast mixed with duration") && ok;

    const xrSim::ActorIntentParseResult mixedWithMemory =
        xrSim::ParseActorIntentPlan("xrsim_actor_intent_v1\ncoast\nmemory player_used_grenade_aggressively\nend\n");
    ok = Expect(!mixedWithMemory.ok, "actor intent parser rejects coast mixed with memory") && ok;

    const xrSim::ActorIntentParseResult mixedWithAction =
        xrSim::ParseActorIntentPlan("xrsim_actor_intent_v1\ncoast\naction move_to cover_node_12\nend\n");
    ok = Expect(!mixedWithAction.ok, "actor intent parser rejects coast mixed with action") && ok;

    return ok;
}

bool TestActorIntentParserRoundTripsFormat()
{
    xrSim::ActorIntentPlan plan;
    plan.goal = "survive_and_delay_player";
    plan.stance = "cautious";
    plan.durationMs = 3500;
    plan.memories.push_back("player_used_grenade_aggressively");
    plan.actions.push_back(xrSim::ActorAction{ "move_to", "cover_node_12", "", "", 0 });
    plan.actions.push_back(xrSim::ActorAction{ "fire_pattern", "target_player", "burst_short", "", 0 });

    const std::string text = xrSim::FormatActorIntentPlan(plan);
    const xrSim::ActorIntentParseResult parsed = xrSim::ParseActorIntentPlan(text);
    bool ok = Expect(parsed.ok, "actor intent parser round-trip format input is valid");
    ok = Expect(parsed.plan.goal == plan.goal, "actor intent round trip preserves goal") && ok;
    ok = Expect(parsed.plan.stance == plan.stance, "actor intent round trip preserves stance") && ok;
    ok = Expect(parsed.plan.durationMs == plan.durationMs, "actor intent round trip preserves duration") && ok;
    ok = Expect(parsed.plan.actions.size() == plan.actions.size(), "actor intent round trip preserves action count") && ok;
    ok = Expect(parsed.plan.memories == plan.memories, "actor intent round trip preserves memories") && ok;
    return ok;
}

bool TestActorIntentParserRejectsBadVersionAsValue()
{
    const xrSim::ActorIntentParseResult parsed = xrSim::ParseActorIntentPlan("xrsim_actor_intent_v2\nend\n");
    bool ok = Expect(!parsed.ok, "actor intent parser rejects bad version as value");
    ok = Expect(parsed.reason.find("version") != std::string::npos, "actor intent parser explains bad version") && ok;
    return ok;
}

bool TestDebugSquadObservationIsExperiential()
{
    xrSim::WorldState state;
    const xrSim::Handle region = state.CreateRegion("debug_region", 100);
    const xrSim::Handle species = state.CreateSpecies("blind_dog");
    xrSim::Result result = state.SetPopulation(region, species, 50);
    bool ok = Expect(result.ok, "squad observation setup accepts population");

    const xrSim::ActorAgentRecord squad = xrSim::MakeDebugSquadAgent();
    const std::string observation = xrSim::BuildActorObservation(squad, state, "player_visible medium_range");

    ok = Expect(observation.find("scope SQUAD") != std::string::npos, "squad observation records scope") && ok;
    ok = Expect(observation.find("memory_summary") != std::string::npos, "squad observation records memory") && ok;
    ok = Expect(observation.find("situation player_visible medium_range") != std::string::npos,
        "squad observation records situation") && ok;
    ok = Expect(observation.find("legal_tools move_to fire_pattern vocalize retreat author_memory adjust_population") !=
            std::string::npos,
        "squad observation records legal tools") && ok;
    ok = Expect(observation.find(state.Digest()) != std::string::npos, "squad observation includes world digest") && ok;
    return ok;
}

bool TestDebugMutantPackObservationUsesPackTools()
{
    xrSim::WorldState state;
    state.CreateRegion("debug_region", 100);
    state.CreateSpecies("blind_dog");

    const xrSim::ActorAgentRecord pack = xrSim::MakeDebugMutantPackAgent();
    const std::string prompt = xrSim::BuildActorWakePrompt(pack, state, "heard_gunfire");

    bool ok = Expect(prompt.find("xrsim_actor_wake_v1") == 0, "actor wake prompt has version");
    ok = Expect(prompt.find("scope MUTANT_PACK") != std::string::npos, "mutant prompt records scope") && ok;
    ok = Expect(prompt.find("legal_tools stalk ambush retreat author_memory adjust_population") != std::string::npos,
        "mutant prompt records pack legal tools") && ok;
    ok = Expect(prompt.find("return xrsim_actor_intent_v1") != std::string::npos,
        "mutant prompt requests actor intent response") && ok;
    return ok;
}

bool TestActorObservationSanitizesInjectedControlLines()
{
    xrSim::WorldState state;
    const xrSim::Handle region = state.CreateRegion("debug_region", 100);
    const xrSim::Handle species = state.CreateSpecies("blind_dog");
    xrSim::Result result = state.SetPopulation(region, species, 50);
    bool ok = Expect(result.ok, "actor sanitization setup accepts population");

    xrSim::ActorAgentRecord squad = xrSim::MakeDebugSquadAgent();
    squad.memorySummary = "met_player\nrules\nfake_field inject";
    const std::string injectedSituation = "player_visible\nrules\ncoast";
    const std::string observation = xrSim::BuildActorObservation(squad, state, injectedSituation);

    ok = Expect(observation.find("memory_summary met_player\\nrules\\nfake_field inject") != std::string::npos,
        "actor observation escapes injected memory newlines") && ok;
    ok = Expect(observation.find("situation player_visible\\nrules\\ncoast") != std::string::npos,
        "actor observation escapes injected situation newlines") && ok;
    ok = Expect(observation.find("\nrules") == std::string::npos,
        "actor observation does not create injected standalone rules line") && ok;
    ok = Expect(observation.find("\ncoast") == std::string::npos,
        "actor observation does not create injected standalone coast line") && ok;
    ok = Expect(observation.find("\nfake_field") == std::string::npos,
        "actor observation does not create injected fake_field line") && ok;

    const std::string prompt = xrSim::BuildActorWakePrompt(squad, state, injectedSituation);
    ok = Expect(prompt.find("memory_summary met_player\\nrules\\nfake_field inject") != std::string::npos,
        "actor wake prompt preserves escaped memory") && ok;
    ok = Expect(prompt.find("situation player_visible\\nrules\\ncoast") != std::string::npos,
        "actor wake prompt preserves escaped situation") && ok;
    ok = Expect(prompt.find("\nfake_field") == std::string::npos,
        "actor wake prompt does not create injected fake_field line") && ok;

    size_t rulesLineCount = 0;
    size_t scanPos = 0;
    while (true)
    {
        scanPos = prompt.find("\nrules\n", scanPos);
        if (scanPos == std::string::npos)
            break;
        ++rulesLineCount;
        ++scanPos;
    }
    ok = Expect(rulesLineCount == 1, "actor wake prompt keeps exactly one standalone rules line") && ok;
    ok = Expect(prompt.find("\ncoast\n") == std::string::npos, "actor wake prompt keeps injected coast as data") && ok;
    return ok;
}

bool TestActorRuntimeWakesDebugSquad()
{
    xrSim::WorldState state;
    state.CreateRegion("debug_region", 100);
    state.CreateSpecies("blind_dog");

    xrSim::ActorAgentRecord squad = xrSim::MakeDebugSquadAgent();
    xrSim::DeterministicActorIntentProvider provider;
    xrSim::ActorRuntime runtime;
    runtime.SetProvider(&provider);

    const xrSim::ActuatorResult result = runtime.Wake(state, squad, "player_visible medium_range", 1);

    bool ok = Expect(result.ok, "actor runtime wakes debug squad");
    ok = Expect(runtime.WakeCount() == 1, "actor runtime records wake count") && ok;
    ok = Expect(runtime.LastCommands().size() >= 2, "actor runtime stores actuator commands") && ok;
    ok = Expect(squad.lastIntent.goal == "survive_and_delay_player", "actor runtime stores last squad intent") && ok;
    return ok;
}

bool TestActorRuntimeWakesDebugMutantPack()
{
    xrSim::WorldState state;
    const xrSim::Handle region = state.CreateRegion("debug_region", 100);
    const xrSim::Handle species = state.CreateSpecies("blind_dog");
    xrSim::Result setup = state.SetPopulation(region, species, 50);
    bool ok = Expect(setup.ok, "mutant runtime setup accepts population");

    xrSim::ActorAgentRecord pack = xrSim::MakeDebugMutantPackAgent();
    xrSim::DeterministicActorIntentProvider provider;
    xrSim::ActorRuntime runtime;
    runtime.SetProvider(&provider);

    const xrSim::ActuatorResult result = runtime.Wake(state, pack, "heard_gunfire", 1);

    ok = Expect(result.ok, "actor runtime wakes debug mutant pack") && ok;
    ok = Expect(pack.lastIntent.goal == "feed_without_losing_alpha", "actor runtime stores pack goal") && ok;
    ok = Expect(runtime.LastCommands().size() >= 1, "actor runtime stores pack commands") && ok;
    return ok;
}

bool TestRecordedActorProviderExhaustionFailsAsValue()
{
    std::vector<std::string> script;
    xrSim::RecordedActorIntentProvider provider(script);
    xrSim::ActorWakeContext context;
    context.actor = xrSim::MakeDebugSquadAgent();

    const xrSim::ActorProviderResult result = provider.Wake(context);
    bool ok = Expect(!result.ok, "recorded actor provider exhaustion fails as value");
    ok = Expect(result.error.find("exhausted") != std::string::npos, "recorded actor provider explains exhaustion") && ok;
    return ok;
}

bool TestActorRuntimeClearsLastCommandsWhenProviderFails()
{
    xrSim::WorldState state;
    state.CreateRegion("debug_region", 100);
    state.CreateSpecies("blind_dog");

    xrSim::ActorAgentRecord squad = xrSim::MakeDebugSquadAgent();
    xrSim::ActorRuntime runtime;
    xrSim::DeterministicActorIntentProvider deterministicProvider;
    runtime.SetProvider(&deterministicProvider);

    xrSim::ActuatorResult result = runtime.Wake(state, squad, "player_visible medium_range", 1);
    bool ok = Expect(result.ok, "provider failure regression setup wake succeeds");
    const xrSim::ActorIntentPlan previousIntent = squad.lastIntent;
    ok = Expect(runtime.LastCommands().size() >= 2, "provider failure regression setup stores commands") && ok;
    ok = Expect(runtime.WakeCount() == 1, "provider failure regression setup records wake count") && ok;

    std::vector<std::string> emptyScript;
    xrSim::RecordedActorIntentProvider exhaustedProvider(emptyScript);
    runtime.SetProvider(&exhaustedProvider);

    result = runtime.Wake(state, squad, "player_visible medium_range", 2);

    ok = Expect(!result.ok, "actor runtime returns value failure when provider is exhausted") && ok;
    ok = Expect(runtime.LastCommands().empty(), "actor runtime clears last commands when provider fails") && ok;
    ok = Expect(runtime.WakeCount() == 1, "actor runtime does not increment wake count on provider failure") && ok;
    ok = Expect(squad.lastIntent.goal == previousIntent.goal, "actor runtime preserves prior intent on provider failure") && ok;
    return ok;
}

bool TestActorRuntimePreservesLastIntentWhenExecutionFails()
{
    xrSim::WorldState state;
    state.CreateRegion("debug_region", 100);
    state.CreateSpecies("blind_dog");

    xrSim::ActorAgentRecord squad = xrSim::MakeDebugSquadAgent();
    xrSim::ActorRuntime runtime;
    xrSim::DeterministicActorIntentProvider deterministicProvider;
    runtime.SetProvider(&deterministicProvider);

    xrSim::ActuatorResult result = runtime.Wake(state, squad, "player_visible medium_range", 1);
    bool ok = Expect(result.ok, "execution failure regression setup wake succeeds");
    const xrSim::ActorIntentPlan previousIntent = squad.lastIntent;
    ok = Expect(runtime.LastCommands().size() >= 2, "execution failure regression setup stores commands") && ok;
    ok = Expect(runtime.WakeCount() == 1, "execution failure regression setup records wake count") && ok;

    std::vector<std::string> script;
    script.push_back(
        "xrsim_actor_intent_v1\n"
        "goal survive_and_delay_player\n"
        "stance reckless\n"
        "duration_ms 1000\n"
        "action stalk target_player crescent\n"
        "end\n");
    xrSim::RecordedActorIntentProvider illegalPlanProvider(script);
    runtime.SetProvider(&illegalPlanProvider);

    result = runtime.Wake(state, squad, "player_visible medium_range", 2);

    ok = Expect(!result.ok, "actor runtime returns value failure when execution rejects plan") && ok;
    ok = Expect(runtime.LastCommands().empty(), "actor runtime clears last commands when execution fails") && ok;
    ok = Expect(runtime.WakeCount() == 1, "actor runtime does not increment wake count on execution failure") && ok;
    ok = Expect(squad.lastIntent.goal == previousIntent.goal, "actor runtime preserves prior intent on execution failure") && ok;
    ok = Expect(squad.lastIntent.stance == previousIntent.stance, "actor runtime keeps previous stance on execution failure") && ok;
    return ok;
}

bool TestActorRuntimeCoastPreservesPriorEmbodiedIntent()
{
    xrSim::WorldState state;
    state.CreateRegion("debug_region", 100);
    state.CreateSpecies("blind_dog");

    xrSim::ActorAgentRecord squad = xrSim::MakeDebugSquadAgent();
    xrSim::ActorRuntime runtime;
    xrSim::DeterministicActorIntentProvider deterministicProvider;
    runtime.SetProvider(&deterministicProvider);

    xrSim::ActuatorResult result = runtime.Wake(state, squad, "player_visible medium_range", 1);
    bool ok = Expect(result.ok, "actor coast regression setup wake succeeds");
    const xrSim::ActorIntentPlan previousIntent = squad.lastIntent;
    ok = Expect(runtime.LastCommands().size() >= 2, "actor coast regression setup stores commands") && ok;
    ok = Expect(runtime.WakeCount() == 1, "actor coast regression setup records initial wake count") && ok;

    std::vector<std::string> script;
    script.push_back(
        "xrsim_actor_intent_v1\n"
        "coast\n"
        "end\n");
    xrSim::RecordedActorIntentProvider coastProvider(script);
    runtime.SetProvider(&coastProvider);

    result = runtime.Wake(state, squad, "player_visible medium_range", 2);

    ok = Expect(result.ok, "actor runtime accepts provider coast") && ok;
    ok = Expect(result.coast, "actor runtime reports coast result") && ok;
    ok = Expect(runtime.LastCommands().empty(), "actor runtime clears commands for coast wake") && ok;
    ok = Expect(runtime.WakeCount() == 1, "actor runtime does not count coast as new embodied wake") && ok;
    ok = Expect(squad.lastIntent.goal == previousIntent.goal, "actor runtime keeps previous goal on coast") && ok;
    ok = Expect(squad.lastIntent.stance == previousIntent.stance, "actor runtime keeps previous stance on coast") && ok;
    return ok;
}

bool TestNullAgentWakeAppliesDeterministicIntent()
{
    xrSim::WorldState state;
    const xrSim::Handle region = state.CreateRegion("debug_region", 100);
    const xrSim::Handle species = state.CreateSpecies("blind_dog");
    xrSim::Result result = state.SetPopulation(region, species, 50);
    bool ok = Expect(result.ok, "null agent setup accepts population");

    xrSim::NullAgentRuntime runtime;
    result = runtime.Wake(state, 3);
    ok = Expect(result.ok, "null agent wake succeeds") && ok;
    ok = Expect(result.appliedDelta == 5, "null agent wake applies deterministic delta") && ok;
    ok = Expect(state.Population(region, species) == 55, "null agent wake mutates population") && ok;
    ok = Expect(state.ToolLog().size() == 1, "null agent wake records tool log") && ok;
    ok = Expect(state.ToolLog().back().seq == 1, "null agent wake starts sequence at one") && ok;
    ok = Expect(state.ToolLog().back().gameDay == 3, "null agent wake records game day") && ok;
    ok = Expect(runtime.WakeCount() == 1, "null agent wake count increments") && ok;
    return ok;
}

bool TestBridgeDebugVerbs()
{
    bool verbOk = false;
    std::string out = xrSim::HandleBridgeVerb("ai.reset", "", verbOk);
    bool ok = Expect(verbOk, "ai.reset succeeds");
    ok = Expect(out.find("xrsim reset") != std::string::npos, "ai.reset reports reset") && ok;

    out = xrSim::HandleBridgeVerb("ai.observe", "", verbOk);
    ok = Expect(verbOk, "ai.observe succeeds") && ok;
    ok = Expect(out == "regions=1 species=1 cohorts=1 log=0 pop{debug_region:blind_dog=50}",
        "ai.observe reports deterministic debug digest") && ok;

    out = xrSim::HandleBridgeVerb("ai.inject", "adjust_population debug_region blind_dog 500", verbOk);
    ok = Expect(verbOk, "ai.inject succeeds") && ok;
    ok = Expect(out == "accepted applied_delta=10", "ai.inject reports clamped delta") && ok;

    out = xrSim::HandleBridgeVerb("ai.observe", "", verbOk);
    ok = Expect(verbOk, "ai.observe after inject succeeds") && ok;
    ok = Expect(out == "regions=1 species=1 cohorts=1 log=1 pop{debug_region:blind_dog=60}",
        "ai.observe after inject reports updated digest") && ok;

    out = xrSim::HandleBridgeVerb("ai.inject", "adjust_population debug_region snork 5", verbOk);
    ok = Expect(!verbOk, "ai.inject unknown species fails") && ok;
    ok = Expect(out.find("unknown species") != std::string::npos, "ai.inject unknown species explains failure") && ok;
    return ok;
}

bool TestBridgeSnapshotVerbs()
{
    bool verbOk = false;
    std::string out = xrSim::HandleBridgeVerb("ai.reset", "", verbOk);
    bool ok = Expect(verbOk, "bridge snapshot setup reset succeeds");

    out = xrSim::HandleBridgeVerb("ai.inject", "adjust_population debug_region blind_dog 500", verbOk);
    ok = Expect(verbOk, "bridge snapshot setup inject succeeds") && ok;

    const std::string snapshot = xrSim::HandleBridgeVerb("ai.snapshot", "", verbOk);
    ok = Expect(verbOk, "ai.snapshot succeeds") && ok;
    ok = Expect(snapshot.find("xrsim_snapshot_v1") == 0, "ai.snapshot returns a versioned snapshot") && ok;

    out = xrSim::HandleBridgeVerb("ai.log", "", verbOk);
    ok = Expect(verbOk, "ai.log succeeds") && ok;
    ok = Expect(out == "seq=1 day=0 tool=adjust_population accepted=1 requested=500 applied=10",
        "ai.log returns deterministic tool log") && ok;

    out = xrSim::HandleBridgeVerb("ai.reset", "", verbOk);
    ok = Expect(verbOk, "bridge snapshot reset before restore succeeds") && ok;
    out = xrSim::HandleBridgeVerb("ai.restore", snapshot, verbOk);
    ok = Expect(verbOk, "ai.restore succeeds") && ok;
    ok = Expect(out == "xrsim restored log=1", "ai.restore reports restored log size") && ok;

    out = xrSim::HandleBridgeVerb("ai.observe", "", verbOk);
    ok = Expect(verbOk, "ai.observe after restore succeeds") && ok;
    ok = Expect(out == "regions=1 species=1 cohorts=1 log=1 pop{debug_region:blind_dog=60}",
        "ai.restore recreates observed digest") && ok;
    return ok;
}

bool TestBridgeReplayVerb()
{
    bool verbOk = false;
    std::string out = xrSim::HandleBridgeVerb("ai.reset", "", verbOk);
    bool ok = Expect(verbOk, "bridge replay setup reset succeeds");
    out = xrSim::HandleBridgeVerb("ai.inject", "adjust_population debug_region blind_dog 500", verbOk);
    ok = Expect(verbOk, "bridge replay setup inject succeeds") && ok;

    out = xrSim::HandleBridgeVerb("ai.replay", "", verbOk);
    ok = Expect(verbOk, "ai.replay succeeds") && ok;
    ok = Expect(out == "xrsim replay digest=regions=1 species=1 cohorts=1 log=1 pop{debug_region:blind_dog=60}",
        "ai.replay reports reproduced digest") && ok;
    return ok;
}

bool TestBridgeNullAgentWakeVerb()
{
    bool verbOk = false;
    std::string out = xrSim::HandleBridgeVerb("ai.reset", "", verbOk);
    bool ok = Expect(verbOk, "bridge null-agent setup reset succeeds");

    out = xrSim::HandleBridgeVerb("ai.wake", "", verbOk);
    ok = Expect(verbOk, "ai.wake succeeds") && ok;
    ok = Expect(out == "xrsim wake applied_delta=5 wakes=1", "ai.wake reports deterministic wake") && ok;

    out = xrSim::HandleBridgeVerb("ai.observe", "", verbOk);
    ok = Expect(verbOk, "ai.observe after wake succeeds") && ok;
    ok = Expect(out == "regions=1 species=1 cohorts=1 log=1 pop{debug_region:blind_dog=55}",
        "ai.wake updates observed digest") && ok;
    return ok;
}

bool TestAgentBridgeAliases()
{
    bool verbOk = false;
    std::string out = xrSim::HandleBridgeVerb("ai.reset", "", verbOk);
    bool ok = Expect(verbOk, "agent alias setup reset succeeds");

    out = xrSim::HandleBridgeVerb("agent.list", "", verbOk);
    ok = Expect(verbOk, "agent.list succeeds") && ok;
    ok = Expect(out == "id=1 scope=ZONE provider=null wakes=0 state=ready", "agent.list reports null zone agent") && ok;

    out = xrSim::HandleBridgeVerb("agent.provider", "", verbOk);
    ok = Expect(verbOk, "agent.provider succeeds") && ok;
    ok = Expect(out == "id=1 provider=null model=deterministic-null", "agent.provider reports provider contract") && ok;

    out = xrSim::HandleBridgeVerb("agent.provider", "live", verbOk);
    ok = Expect(verbOk, "agent.provider live succeeds") && ok;
    ok = Expect(out.find("provider=anthropic") != std::string::npos, "agent.provider live reports provider") && ok;
    ok = Expect(out.find("model=claude-sonnet-5") != std::string::npos, "agent.provider live reports sonnet model") && ok;
    ok = Expect(out.find("transport=") != std::string::npos, "agent.provider live reports transport") && ok;

    out = xrSim::HandleBridgeVerb("agent.prompt", "", verbOk);
    ok = Expect(verbOk, "agent.prompt succeeds") && ok;
    ok = Expect(out.find("xrsim_agent_wake_v1") == 0, "agent.prompt returns versioned prompt") && ok;
    ok = Expect(out.find("regions=1 species=1 cohorts=1 log=0 pop{debug_region:blind_dog=50}") != std::string::npos,
        "agent.prompt includes current observation") && ok;

    out = xrSim::HandleBridgeVerb("agent.wake", "1", verbOk);
    ok = Expect(verbOk, "agent.wake succeeds") && ok;
    ok = Expect(out == "xrsim wake applied_delta=5 wakes=1", "agent.wake maps to deterministic null wake") && ok;

    out = xrSim::HandleBridgeVerb("agent.tree", "", verbOk);
    ok = Expect(verbOk, "agent.tree succeeds") && ok;
    ok = Expect(out == "ZONE#1 provider=null wakes=1 log=1", "agent.tree reports deterministic tree shape") && ok;

    {
        ScopedEnvVar provider("XRAY_AGENT_PROVIDER", "off");
        ScopedEnvVar model("XRAY_AGENT_MODEL", "claude-sonnet-5-test");
        out = xrSim::HandleBridgeVerb("agent.wake", "live", verbOk);
    }
    ok = Expect(verbOk, "agent.wake live succeeds when provider coasts") && ok;
    ok = Expect(out == "xrsim live wake applied_delta=0 wakes=2 provider=off model=claude-sonnet-5-test coast=provider_disabled",
        "agent.wake live routes through live provider config") && ok;
    return ok;
}

bool TestActorBridgeVerbs()
{
    bool verbOk = false;
    std::string out = xrSim::HandleBridgeVerb("ai.reset", "", verbOk);
    bool ok = Expect(verbOk, "actor bridge setup reset succeeds");

    out = xrSim::HandleBridgeVerb("agent.actor.list", "", verbOk);
    ok = Expect(verbOk, "agent.actor.list succeeds") && ok;
    ok = Expect(out.find("SQUAD#101") != std::string::npos, "agent.actor.list reports squad") && ok;
    ok = Expect(out.find("MUTANT_PACK#201") != std::string::npos, "agent.actor.list reports mutant pack") && ok;

    out = xrSim::HandleBridgeVerb("agent.actor.observe", "squad", verbOk);
    ok = Expect(verbOk, "agent.actor.observe squad succeeds") && ok;
    ok = Expect(out.find("scope SQUAD") != std::string::npos, "agent.actor.observe squad reports scope") && ok;

    out = xrSim::HandleBridgeVerb("agent.actor.wake", "squad", verbOk);
    ok = Expect(verbOk, "agent.actor.wake squad succeeds") && ok;
    ok = Expect(out.find("goal=survive_and_delay_player") != std::string::npos,
        "agent.actor.wake squad reports goal") && ok;

    out = xrSim::HandleBridgeVerb("agent.actor.wake", "mutant_pack", verbOk);
    ok = Expect(verbOk, "agent.actor.wake mutant_pack succeeds") && ok;
    ok = Expect(out.find("goal=feed_without_losing_alpha") != std::string::npos,
        "agent.actor.wake mutant pack reports goal") && ok;

    out = xrSim::HandleBridgeVerb("agent.actor.commands", "", verbOk);
    ok = Expect(verbOk, "agent.actor.commands succeeds") && ok;
    ok = Expect(out != "empty", "agent.actor.commands reports actuator stream") && ok;
    return ok;
}

bool TestAnthropicTextResponseParsesPrettyPrintedUnicodeTextBlock()
{
    const char* response =
        "{\n"
        "  \"id\" : \"msg_test\",\n"
        "  \"type\" : \"message\",\n"
        "  \"content\" : [\n"
        "    {\n"
        "      \"type\" : \"tool_use\",\n"
        "      \"name\" : \"ignore_me\"\n"
        "    },\n"
        "    {\n"
        "      \"type\" : \"text\",\n"
        "      \"text\" : \"xrsim_agent_response_v1\\nintent adjust_population debug_region blind_dog \\u0033\\nend\\n\"\n"
        "    }\n"
        "  ]\n"
        "}\n";

    const xrSim::AgentProviderResult result =
        xrSim::ParseAnthropicMessagesTextResponse(response, "anthropic", "claude-sonnet-5");
    bool ok = Expect(result.ok, "anthropic text response parses pretty printed messages json");
    ok = Expect(result.intents.size() == 1, "anthropic pretty printed response emits one intent") && ok;
    if (result.intents.size() == 1)
        ok = Expect(result.intents[0].delta == 3, "anthropic text response decodes unicode escapes in text") && ok;
    return ok;
}

bool TestActorObservationIncludesLastIntentSummary()
{
    xrSim::WorldState state;
    const xrSim::Handle region = state.CreateRegion("debug_region", 100);
    const xrSim::Handle species = state.CreateSpecies("blind_dog");
    xrSim::Result setup = state.SetPopulation(region, species, 50);
    bool ok = Expect(setup.ok, "actor observation last intent setup accepts population");

    xrSim::ActorAgentRecord squad = xrSim::MakeDebugSquadAgent();
    squad.lastIntent.goal = "survive_and_delay_player";
    squad.lastIntent.stance = "cautious";
    squad.lastIntent.durationMs = 3500;
    squad.lastIntent.actions.push_back(xrSim::ActorAction{ "move_to", "cover_node_12", "", "", 0 });
    squad.lastIntent.actions.push_back(xrSim::ActorAction{ "fire_pattern", "target_player", "burst_short", "", 0 });

    const std::string observation = xrSim::BuildActorObservation(squad, state, "player_visible medium_range");
    ok = Expect(observation.find("current_goal survive_and_delay_player") != std::string::npos,
        "actor observation includes current goal derived from last intent") && ok;
    ok = Expect(observation.find("last_intent goal=survive_and_delay_player stance=cautious") != std::string::npos,
        "actor observation includes one-line last intent summary") && ok;
    return ok;
}

bool TestPackSpawnPayloadParserAcceptsDefaults()
{
    const xrSim::PackSpawnParseResult parsed = xrSim::ParsePackSpawnPayload("dog_weak 3");
    bool ok = Expect(parsed.ok, "pack spawn parser accepts section and count");
    ok = Expect(parsed.request.section == "dog_weak", "pack spawn parser captures section") && ok;
    ok = Expect(parsed.request.count == 3, "pack spawn parser captures count") && ok;
    ok = Expect(parsed.request.radiusMeters == 8, "pack spawn parser defaults radius") && ok;
    return ok;
}

bool TestPackSpawnPayloadParserRejectsUnsafeValues()
{
    bool ok = true;
    ok = Expect(!xrSim::ParsePackSpawnPayload("dog_weak 0").ok, "pack spawn parser rejects zero count") && ok;
    ok = Expect(!xrSim::ParsePackSpawnPayload("dog_weak 9").ok, "pack spawn parser rejects too many members") && ok;
    ok = Expect(!xrSim::ParsePackSpawnPayload("dog_weak 2 0").ok, "pack spawn parser rejects zero radius") && ok;
    ok = Expect(!xrSim::ParsePackSpawnPayload("dog_weak 2 41").ok, "pack spawn parser rejects too large radius") && ok;
    ok = Expect(!xrSim::ParsePackSpawnPayload("dog_weak 2 8 trailing").ok, "pack spawn parser rejects trailing tokens") && ok;
    return ok;
}

bool TestSessionPackRegistryRegistersIdsAndObserves()
{
    xrSim::ResetSessionPacksForTests();
    xrSim::PackRegistration registration;
    registration.section = "dog_weak";
    registration.objectIds = {1010, 1011, 1012};
    registration.spawnReason = "session_spawn";

    const xrSim::PackRegisterResult result = xrSim::RegisterSessionMutantPack(registration);
    bool ok = Expect(result.ok, "pack registry accepts valid registration");
    ok = Expect(result.packId == 1, "pack registry allocates first pack id") && ok;
    ok = Expect(xrSim::FormatPackList().find("PACK#1") != std::string::npos, "pack list includes id") && ok;
    ok = Expect(xrSim::FormatPackList().find("members=3") != std::string::npos, "pack list includes member count") && ok;
    return ok;
}

bool TestSessionPackWakeUsesMutantPackIntent()
{
    xrSim::ResetSessionPacksForTests();
    xrSim::PackRegistration registration;
    registration.section = "dog_weak";
    registration.objectIds = {1010, 1011, 1012};
    const xrSim::PackRegisterResult registered = xrSim::RegisterSessionMutantPack(registration);

    xrSim::WorldState world;
    const xrSim::Handle region = world.CreateRegion("debug_region", 100);
    const xrSim::Handle species = world.CreateSpecies("blind_dog");
    bool ok = Expect(world.SetPopulation(region, species, 50).ok, "pack wake setup accepts population");

    xrSim::ActorRuntime runtime;
    const xrSim::ActuatorResult wake = xrSim::WakePack(registered.packId, world, runtime, 0);
    ok = Expect(wake.ok, "pack wake succeeds") && ok;
    ok = Expect(runtime.LastProviderResult().plan.goal == "feed_without_losing_alpha",
        "pack wake uses mutant pack deterministic intent") && ok;
    ok = Expect(!wake.commands.empty(), "pack wake emits actuator commands") && ok;
    return ok;
}

bool TestPackBridgeVerbsRegisterObserveWake()
{
    bool verbOk = false;
    std::string out = xrSim::HandleBridgeVerb("ai.reset", "", verbOk);
    bool ok = Expect(verbOk, "pack bridge reset succeeds");

    out = xrSim::HandleBridgeVerb("agent.pack.register", "dog_weak 1010 1011 1012", verbOk);
    ok = Expect(verbOk, "agent.pack.register succeeds") && ok;
    ok = Expect(out.find("PACK#1") != std::string::npos, "agent.pack.register reports pack id") && ok;

    out = xrSim::HandleBridgeVerb("agent.pack.list", "", verbOk);
    ok = Expect(verbOk, "agent.pack.list succeeds") && ok;
    ok = Expect(out.find("members=3") != std::string::npos, "agent.pack.list reports members") && ok;

    out = xrSim::HandleBridgeVerb("agent.pack.observe", "1", verbOk);
    ok = Expect(verbOk, "agent.pack.observe succeeds") && ok;
    ok = Expect(out.find("scope MUTANT_PACK") != std::string::npos, "agent.pack.observe reports mutant scope") && ok;
    ok = Expect(out.find("members=3") != std::string::npos, "agent.pack.observe reports members") && ok;

    out = xrSim::HandleBridgeVerb("agent.pack.wake", "1", verbOk);
    ok = Expect(verbOk, "agent.pack.wake succeeds") && ok;
    ok = Expect(out.find("goal=feed_without_losing_alpha") != std::string::npos, "agent.pack.wake reports goal") && ok;

    out = xrSim::HandleBridgeVerb("agent.pack.commands", "", verbOk);
    ok = Expect(verbOk, "agent.pack.commands succeeds") && ok;
    ok = Expect(out != "empty", "agent.pack.commands reports commands") && ok;
    return ok;
}

bool TestPackBridgeRegisterInitializesBeforeSessionPack()
{
    bool verbOk = false;
    std::string out = xrSim::HandleBridgeVerb("agent.pack.register", "dog_weak 2010 2011 2012", verbOk);
    bool ok = Expect(verbOk, "agent.pack.register succeeds before explicit ai.reset");
    ok = Expect(out.find("PACK#1") != std::string::npos, "first register before reset reports pack id") && ok;

    out = xrSim::HandleBridgeVerb("agent.pack.observe", "1", verbOk);
    ok = Expect(verbOk, "agent.pack.observe keeps first registered pack after debug world init") && ok;
    ok = Expect(out.find("members=3") != std::string::npos, "first registered pack remains observable") && ok;

    xrSim::HandleBridgeVerb("ai.reset", "", verbOk);
    return ok && Expect(verbOk, "pack bridge no-reset regression cleanup succeeds");
}
} // namespace

int main()
{
    bool ok = true;
    ok = TestAdjustPopulationClampsAndLogs() && ok;
    ok = TestMissingHandleFailsAsValue() && ok;
    ok = TestDigestIsStable() && ok;
    ok = TestSnapshotRoundTripPreservesStateAndLog() && ok;
    ok = TestSnapshotLoadRejectsBadVersionAsValue() && ok;
    ok = TestReplayToolLogReproducesRecordedDigest() && ok;
    ok = TestReplayDetectsDivergentBaseline() && ok;
    ok = TestNullProviderReturnsDeterministicIntent() && ok;
    ok = TestRuntimeUsesRecordedProviderIntent() && ok;
    ok = TestRecordedProviderExhaustionFailsAsValue() && ok;
    ok = TestAgentPromptIncludesObservationAndToolSchema() && ok;
    ok = TestAgentResponseParserAcceptsIntentLines() && ok;
    ok = TestAgentResponseParserRejectsUnknownIntentAsValue() && ok;
    ok = TestAgentResponseParserAcceptsCoastAsNormalValue() && ok;
    ok = TestAgentResponseParserAcceptsEchoedMetadataBeforeCoast() && ok;
    ok = TestAgentResponseParserAcceptsActionCoastWithReason() && ok;
    ok = TestAgentResponseParserFindsVersionAfterModelPreface() && ok;
    ok = TestActuatorConvertsSquadIntentToCommands() && ok;
    ok = TestActuatorRejectsIllegalPackVerbAsValue() && ok;
    ok = TestActuatorAppliesPopulationToolThroughWorldState() && ok;
    ok = TestActuatorRejectsMixedPlanAtomically() && ok;
    ok = TestRecordedTextProviderParsesScriptedWake() && ok;
    ok = TestRuntimeCoastsOnProviderCoastResult() && ok;
    ok = TestAgentProviderConfigDefaultsToSonnetTier() && ok;
    ok = TestAgentProviderConfigReadsEnvironmentValues() && ok;
    ok = TestAnthropicProviderShellCoastsWithoutApiKey() && ok;
    ok = TestAgentProviderLedgerFormatsReplayMetadata() && ok;
    ok = TestAnthropicRequestEnvelopeUsesMessagesApiShape() && ok;
    ok = TestAnthropicTextResponseParsesThroughAgentCodec() && ok;
    ok = TestAnthropicProviderShellUsesInjectedTransport() && ok;
    ok = TestAnthropicProviderShellCoastsOnTransportFailure() && ok;
    ok = TestAnthropicActorProviderParsesInjectedTransportIntent() && ok;
    ok = TestAnthropicActorProviderCoastsWithoutApiKey() && ok;
    ok = TestAnthropicActorProviderCoastsOnTransportFailure() && ok;
    ok = TestAsyncActorProviderQueueRunsWakeOffCallerThread() && ok;
    ok = TestAsyncActorProviderQueueRejectsSubmitAfterShutdown() && ok;
    ok = TestAnthropicHttpTransportFactoryMatchesAvailability() && ok;
    ok = TestActorIntentParserAcceptsSquadPlan() && ok;
    ok = TestActorIntentParserAcceptsCoast() && ok;
    ok = TestActorIntentParserSupportsVerbOnlyActionFormatRoundTrip() && ok;
    ok = TestActorIntentParserRoutesAdjustPopulationAmountIntoActuator() && ok;
    ok = TestActorIntentParserRejectsMalformedAdjustPopulationAction() && ok;
    ok = TestActorIntentParserRejectsTrailingTokens() && ok;
    ok = TestActorIntentParserRejectsMixedCoastAndMetadataOrActions() && ok;
    ok = TestActorIntentParserRoundTripsFormat() && ok;
    ok = TestActorIntentParserRejectsBadVersionAsValue() && ok;
    ok = TestDebugSquadObservationIsExperiential() && ok;
    ok = TestDebugMutantPackObservationUsesPackTools() && ok;
    ok = TestActorObservationIncludesLastIntentSummary() && ok;
    ok = TestActorObservationSanitizesInjectedControlLines() && ok;
    ok = TestActorRuntimeWakesDebugSquad() && ok;
    ok = TestActorRuntimeWakesDebugMutantPack() && ok;
    ok = TestRecordedActorProviderExhaustionFailsAsValue() && ok;
    ok = TestActorRuntimeClearsLastCommandsWhenProviderFails() && ok;
    ok = TestActorRuntimePreservesLastIntentWhenExecutionFails() && ok;
    ok = TestActorRuntimeCoastPreservesPriorEmbodiedIntent() && ok;
    ok = TestNullAgentWakeAppliesDeterministicIntent() && ok;
    ok = TestPackBridgeRegisterInitializesBeforeSessionPack() && ok;
    ok = TestBridgeDebugVerbs() && ok;
    ok = TestBridgeSnapshotVerbs() && ok;
    ok = TestBridgeReplayVerb() && ok;
    ok = TestBridgeNullAgentWakeVerb() && ok;
    ok = TestAgentBridgeAliases() && ok;
    ok = TestActorBridgeVerbs() && ok;
    ok = TestAnthropicTextResponseParsesPrettyPrintedUnicodeTextBlock() && ok;
    ok = TestPackSpawnPayloadParserAcceptsDefaults() && ok;
    ok = TestPackSpawnPayloadParserRejectsUnsafeValues() && ok;
    ok = TestSessionPackRegistryRegistersIdsAndObserves() && ok;
    ok = TestSessionPackWakeUsesMutantPackIntent() && ok;
    ok = TestPackBridgeVerbsRegisterObserveWake() && ok;
    return ok ? 0 : 1;
}
