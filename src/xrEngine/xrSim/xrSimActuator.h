#pragma once

#include "xrSim/xrSimActors.h"

#include <cstdint>
#include <string>
#include <vector>

namespace xrSim
{
struct ActuatorCommand
{
    std::string verb;
    std::string target;
    std::string arg0;
    std::string arg1;
};

struct ActuatorResult
{
    bool ok = false;
    bool coast = false;
    int32_t appliedDelta = 0;
    std::string reason;
    std::vector<ActuatorCommand> commands;
};

ActuatorResult ExecuteActorIntent(
    WorldState& world, const ActorAgentRecord& actor, const ActorIntentPlan& plan, uint32_t gameDay);
std::string FormatActuatorCommands(const std::vector<ActuatorCommand>& commands);
} // namespace xrSim
