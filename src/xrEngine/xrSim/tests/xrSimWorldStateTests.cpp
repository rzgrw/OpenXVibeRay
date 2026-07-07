#include "xrSim/xrSimAgentProvider.h"
#include "xrSim/xrSimWorldState.h"
#include "xrSim/xrSimBridge.h"
#include "xrSim/xrSimNullAgent.h"

#include <cstdio>
#include <string>
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
        ++calls;
        return result;
    }

    xrSim::AnthropicTransportResult result;
    std::string seenModel;
    std::string seenPath;
    uint32_t calls = 0;
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
    return ok;
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
    ok = TestNullAgentWakeAppliesDeterministicIntent() && ok;
    ok = TestBridgeDebugVerbs() && ok;
    ok = TestBridgeSnapshotVerbs() && ok;
    ok = TestBridgeReplayVerb() && ok;
    ok = TestBridgeNullAgentWakeVerb() && ok;
    ok = TestAgentBridgeAliases() && ok;
    return ok ? 0 : 1;
}
