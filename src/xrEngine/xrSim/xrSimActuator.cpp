#include "xrSim/xrSimActuator.h"

#include <sstream>

namespace xrSim
{
namespace
{
bool IsLegalAction(ActorAgentScope scope, const std::string& verb)
{
    if (verb == "author_memory" || verb == "adjust_population")
        return true;
    if (scope == ActorAgentScope::Squad)
        return verb == "move_to" || verb == "fire_pattern" || verb == "vocalize" || verb == "retreat";
    if (scope == ActorAgentScope::MutantPack)
        return verb == "stalk" || verb == "ambush" || verb == "retreat";
    return false;
}

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
    ActuatorResult result;
    if (plan.coast)
    {
        result.ok = true;
        result.coast = true;
        result.reason = "coast";
        return result;
    }

    if (!plan.stance.empty())
        result.commands.push_back(ActuatorCommand{ "set_stance", plan.stance, "", "" });

    for (const ActorAction& action : plan.actions)
    {
        if (!IsLegalAction(actor.scope, action.verb))
            return FailActuator("illegal action for scope: " + action.verb);

        if (action.verb == "adjust_population")
        {
            const Handle region = world.FindRegionByName(action.target);
            if (!region.IsValid())
                return FailActuator("unknown region: " + action.target);
            const Handle species = world.FindSpeciesByName(action.arg0);
            if (!species.IsValid())
                return FailActuator("unknown species: " + action.arg0);
            const uint32_t seq = uint32_t(world.ToolLog().size() + 1);
            const Result applied = world.ApplyAdjustPopulation(seq, region, species, action.amount, gameDay);
            if (!applied.ok)
                return FailActuator(applied.reason);
            result.appliedDelta += applied.appliedDelta;
            continue;
        }

        result.commands.push_back(ActuatorCommand{ action.verb, action.target, action.arg0, action.arg1 });
    }

    result.ok = true;
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
