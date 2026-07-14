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

ActorWakeContext ActorRuntime::PrepareWake(const ActorAgentRecord& actor, const WorldState& world,
    const std::string& situation, uint32_t gameDay) const
{
    ActorWakeContext context;
    context.actor = actor;
    context.gameDay = gameDay;
    context.observation = BuildActorObservation(actor, world, situation);
    context.prompt = BuildActorWakePrompt(actor, world, situation);
    return context;
}

ActuatorResult ActorRuntime::ApplyProviderResult(WorldState& world, ActorAgentRecord& actor,
    const ActorProviderResult& providerResult, uint32_t gameDay)
{
    m_lastProviderResult = providerResult;
    if (!m_lastProviderResult.ok)
    {
        m_lastCommands.clear();
        m_actorCommands.erase(actor.agentId);
        ActuatorResult failed;
        failed.reason = m_lastProviderResult.error.empty() ? "actor provider failed" : m_lastProviderResult.error;
        return failed;
    }

    if (m_lastProviderResult.coast || m_lastProviderResult.plan.coast)
    {
        const auto previous = m_actorCommands.find(actor.agentId);
        if (previous == m_actorCommands.end())
            m_lastCommands.clear();
        else
            m_lastCommands = previous->second;

        ActuatorResult coast;
        coast.ok = true;
        coast.coast = true;
        coast.reason = m_lastProviderResult.coastReason.empty() ? "coast" : m_lastProviderResult.coastReason;
        coast.commands = m_lastCommands;
        return coast;
    }

    m_lastCommands.clear();
    m_actorCommands.erase(actor.agentId);
    ActuatorResult executed = ExecuteActorIntent(world, actor, m_lastProviderResult.plan, gameDay);
    if (executed.ok)
    {
        actor.lastIntent = m_lastProviderResult.plan;
        m_lastCommands = executed.commands;
        m_actorCommands[actor.agentId] = m_lastCommands;
        ++m_wakeCount;
    }
    return executed;
}

ActuatorResult ActorRuntime::Wake(
    WorldState& world, ActorAgentRecord& actor, const std::string& situation, uint32_t gameDay)
{
    const ActorWakeContext context = PrepareWake(actor, world, situation, gameDay);

    DeterministicActorIntentProvider defaultProvider;
    IActorIntentProvider* provider = m_provider ? m_provider : &defaultProvider;
    return ApplyProviderResult(world, actor, provider->Wake(context), gameDay);
}

uint32_t ActorRuntime::WakeCount() const { return m_wakeCount; }

const std::vector<ActuatorCommand>& ActorRuntime::LastCommands() const { return m_lastCommands; }

const ActorProviderResult& ActorRuntime::LastProviderResult() const { return m_lastProviderResult; }
} // namespace xrSim
