#pragma once

#include "xrSim/xrSimActuator.h"

#include <unordered_map>

namespace xrSim
{
struct ActorWakeContext
{
    ActorAgentRecord actor;
    uint32_t gameDay = 0;
    std::string observation;
    std::string prompt;
};

struct ActorProviderResult
{
    bool ok = false;
    bool coast = false;
    std::string provider;
    std::string model;
    std::string error;
    std::string coastReason;
    ActorIntentPlan plan;
};

class IActorIntentProvider
{
public:
    virtual ~IActorIntentProvider() = default;
    virtual ActorProviderResult Wake(const ActorWakeContext& context) = 0;
    virtual void Cancel() {}
};

class DeterministicActorIntentProvider : public IActorIntentProvider
{
public:
    ActorProviderResult Wake(const ActorWakeContext& context) override;
};

class RecordedActorIntentProvider : public IActorIntentProvider
{
public:
    explicit RecordedActorIntentProvider(const std::vector<std::string>& script);
    ActorProviderResult Wake(const ActorWakeContext& context) override;

private:
    std::vector<std::string> m_script;
    size_t m_cursor = 0;
};

class ActorRuntime
{
public:
    void SetProvider(IActorIntentProvider* provider);
    ActorWakeContext PrepareWake(const ActorAgentRecord& actor, const WorldState& world,
        const std::string& situation, uint32_t gameDay) const;
    ActuatorResult ApplyProviderResult(WorldState& world, ActorAgentRecord& actor,
        const ActorProviderResult& providerResult, uint32_t gameDay);
    ActuatorResult Wake(WorldState& world, ActorAgentRecord& actor, const std::string& situation, uint32_t gameDay);
    uint32_t WakeCount() const;
    const std::vector<ActuatorCommand>& LastCommands() const;
    const ActorProviderResult& LastProviderResult() const;

private:
    IActorIntentProvider* m_provider = nullptr;
    ActorProviderResult m_lastProviderResult;
    std::vector<ActuatorCommand> m_lastCommands;
    std::unordered_map<uint32_t, std::vector<ActuatorCommand>> m_actorCommands;
    uint32_t m_wakeCount = 0;
};
} // namespace xrSim
