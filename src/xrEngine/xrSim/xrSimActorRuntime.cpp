#include "xrSim/xrSimActorRuntime.h"

namespace xrSim
{
namespace
{
ActorProviderResult ParseProviderText(const std::string& provider, const std::string& model, const std::string& text)
{
    const ActorIntentParseResult parsed = ParseActorIntentPlan(text);
    ActorProviderResult result;
    result.ok = parsed.ok;
    result.provider = provider;
    result.model = model;
    result.plan = parsed.plan;
    result.coast = parsed.ok && parsed.plan.coast;
    result.error = parsed.reason;
    return result;
}
} // namespace

ActorProviderResult DeterministicActorIntentProvider::Wake(const ActorWakeContext& context)
{
    if (context.actor.scope == ActorAgentScope::MutantPack)
    {
        return ParseProviderText("deterministic-actor", "fixture",
            "xrsim_actor_intent_v1\n"
            "goal feed_without_losing_alpha\n"
            "stance hungry_but_cautious\n"
            "duration_ms 2500\n"
            "action stalk target_player crescent\n"
            "memory heard_gunfire_near_den\n"
            "end\n");
    }

    return ParseProviderText("deterministic-actor", "fixture",
        "xrsim_actor_intent_v1\n"
        "goal survive_and_delay_player\n"
        "stance cautious\n"
        "duration_ms 3500\n"
        "action move_to cover_node_12\n"
        "action fire_pattern target_player burst_short\n"
        "memory player_used_grenade_aggressively\n"
        "end\n");
}

RecordedActorIntentProvider::RecordedActorIntentProvider(const std::vector<std::string>& script) : m_script(script) {}

ActorProviderResult RecordedActorIntentProvider::Wake(const ActorWakeContext& context)
{
    (void)context;
    if (m_cursor >= m_script.size())
    {
        ActorProviderResult result;
        result.ok = false;
        result.provider = "recorded-actor";
        result.model = "fixture";
        result.error = "recorded actor provider exhausted";
        return result;
    }
    return ParseProviderText("recorded-actor", "fixture", m_script[m_cursor++]);
}

void ActorRuntime::SetProvider(IActorIntentProvider* provider) { m_provider = provider; }

ActuatorResult ActorRuntime::Wake(
    WorldState& world, ActorAgentRecord& actor, const std::string& situation, uint32_t gameDay)
{
    m_lastCommands.clear();

    ActorWakeContext context;
    context.actor = actor;
    context.observation = BuildActorObservation(actor, world, situation);
    context.prompt = BuildActorWakePrompt(actor, world, situation);

    DeterministicActorIntentProvider defaultProvider;
    IActorIntentProvider* provider = m_provider ? m_provider : &defaultProvider;
    m_lastProviderResult = provider->Wake(context);
    if (!m_lastProviderResult.ok)
    {
        ActuatorResult failed;
        failed.reason = m_lastProviderResult.error.empty() ? "actor provider failed" : m_lastProviderResult.error;
        return failed;
    }

    ActuatorResult executed = ExecuteActorIntent(world, actor, m_lastProviderResult.plan, gameDay);
    if (executed.ok && !executed.coast)
    {
        actor.lastIntent = m_lastProviderResult.plan;
        m_lastCommands = executed.commands;
        ++m_wakeCount;
    }
    else if (executed.ok)
    {
        m_lastCommands = executed.commands;
    }
    return executed;
}

uint32_t ActorRuntime::WakeCount() const { return m_wakeCount; }

const std::vector<ActuatorCommand>& ActorRuntime::LastCommands() const { return m_lastCommands; }

const ActorProviderResult& ActorRuntime::LastProviderResult() const { return m_lastProviderResult; }
} // namespace xrSim
