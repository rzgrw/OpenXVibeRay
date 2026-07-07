#include "xrSim/xrSimActuator.h"

#include <sstream>

namespace xrSim
{
namespace
{
ActuatorResult FailActuator(const std::string& reason)
{
    ActuatorResult result;
    result.reason = reason;
    return result;
}
} // namespace

ActuatorResult ExecuteActorIntent(
    WorldState& world, const ActorAgentRecord& actor, const ActorIntentPlan& plan, uint32_t gameDay)
{
    if (plan.coast)
    {
        ActuatorResult result;
        result.ok = true;
        result.coast = true;
        result.reason = "coast";
        return result;
    }

    WorldState stagedWorld = world;
    std::vector<ActuatorCommand> stagedCommands;
    int32_t stagedAppliedDelta = 0;

    if (!plan.stance.empty())
        stagedCommands.push_back(ActuatorCommand{ "set_stance", plan.stance, "", "" });

    for (const ActorAction& action : plan.actions)
    {
        if (!IsActorActionLegal(actor.scope, action.verb))
            return FailActuator("illegal action for scope: " + action.verb);

        if (action.verb == "adjust_population")
        {
            const Handle region = stagedWorld.FindRegionByName(action.target);
            if (!region.IsValid())
                return FailActuator("unknown region: " + action.target);
            const Handle species = stagedWorld.FindSpeciesByName(action.arg0);
            if (!species.IsValid())
                return FailActuator("unknown species: " + action.arg0);
            const uint32_t seq = uint32_t(stagedWorld.ToolLog().size() + 1);
            const Result applied = stagedWorld.ApplyAdjustPopulation(seq, region, species, action.amount, gameDay);
            if (!applied.ok)
                return FailActuator(applied.reason);
            stagedAppliedDelta += applied.appliedDelta;
            continue;
        }

        stagedCommands.push_back(ActuatorCommand{ action.verb, action.target, action.arg0, action.arg1 });
    }

    world = stagedWorld;

    ActuatorResult result;
    result.ok = true;
    result.appliedDelta = stagedAppliedDelta;
    result.commands = stagedCommands;
    return result;
}

std::string FormatActuatorCommands(const std::vector<ActuatorCommand>& commands)
{
    if (commands.empty())
        return "empty";

    std::ostringstream out;
    for (size_t i = 0; i < commands.size(); ++i)
    {
        const ActuatorCommand& command = commands[i];
        if (i != 0)
            out << " | ";
        out << command.verb;
        if (!command.target.empty())
            out << " " << command.target;
        if (!command.arg0.empty())
            out << " " << command.arg0;
        if (!command.arg1.empty())
            out << " " << command.arg1;
    }
    return out.str();
}
} // namespace xrSim
