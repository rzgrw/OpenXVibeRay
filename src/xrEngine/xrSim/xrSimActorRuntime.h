#pragma once

#include "xrSim/xrSimActuator.h"

namespace xrSim
{
struct ActorWakeContext
{
    ActorAgentRecord actor;
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
    ActorIntentPlan plan;
};

class IActorIntentProvider
{
public:
    virtual ~IActorIntentProvider() = default;
    virtual ActorProviderResult Wake(const ActorWakeContext& context) = 0;
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
    ActuatorResult Wake(WorldState& world, ActorAgentRecord& actor, const std::string& situation, uint32_t gameDay);
    uint32_t WakeCount() const;
    const std::vector<ActuatorCommand>& LastCommands() const;
    const ActorProviderResult& LastProviderResult() const;

private:
    IActorIntentProvider* m_provider = nullptr;
    ActorProviderResult m_lastProviderResult;
    std::vector<ActuatorCommand> m_lastCommands;
    uint32_t m_wakeCount = 0;
};
} // namespace xrSim
