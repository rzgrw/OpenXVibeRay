#include "xrSim/xrSimActorIntent.h"

#include <cerrno>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace xrSim
{
namespace
{
ActorIntentParseResult FailActorIntent(const std::string& reason)
{
    ActorIntentParseResult result;
    result.reason = reason;
    return result;
}

bool ParseUint32Token(const std::string& text, uint32_t& value)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
    if (errno != 0 || !end || *end != '\0' || parsed > std::numeric_limits<uint32_t>::max())
        return false;
    value = uint32_t(parsed);
    return true;
}

bool ParseInt32Token(const std::string& text, int32_t& value)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (errno != 0 || !end || *end != '\0' || parsed < std::numeric_limits<int32_t>::min() ||
        parsed > std::numeric_limits<int32_t>::max())
        return false;
    value = int32_t(parsed);
    return true;
}
} // namespace

ActorIntentParseResult ParseActorIntentPlan(const std::string& text)
{
    std::istringstream input(text);
    std::string line;
    if (!std::getline(input, line) || line != "xrsim_actor_intent_v1")
        return FailActorIntent("unsupported actor intent version");

    ActorIntentParseResult result;
    result.ok = true;

    bool sawEnd = false;
    while (std::getline(input, line))
    {
        if (line.empty())
            continue;
        if (line == "end")
        {
            sawEnd = true;
            break;
        }
        if (line == "coast")
        {
            if (!result.plan.actions.empty())
                return FailActorIntent("coast cannot be mixed with actions");
            result.plan.coast = true;
            continue;
        }

        std::istringstream record(line);
        std::string key;
        record >> key;
        if (key == "goal")
        {
            if (!(record >> result.plan.goal))
                return FailActorIntent("invalid goal record");
        }
        else if (key == "stance")
        {
            if (!(record >> result.plan.stance))
                return FailActorIntent("invalid stance record");
        }
        else if (key == "duration_ms")
        {
            std::string value;
            if (!(record >> value) || !ParseUint32Token(value, result.plan.durationMs))
                return FailActorIntent("invalid duration_ms record");
        }
        else if (key == "action")
        {
            if (result.plan.coast)
                return FailActorIntent("coast cannot be mixed with actions");
            ActorAction action;
            std::string amountText;
            record >> action.verb >> action.target >> action.arg0 >> action.arg1 >> amountText;
            if (action.verb.empty())
                return FailActorIntent("invalid action record");
            if (!amountText.empty() && !ParseInt32Token(amountText, action.amount))
                return FailActorIntent("invalid action amount");
            result.plan.actions.push_back(action);
        }
        else if (key == "memory")
        {
            std::string memory;
            if (!(record >> memory))
                return FailActorIntent("invalid memory record");
            result.plan.memories.push_back(memory);
        }
        else
        {
            return FailActorIntent("unknown actor intent record: " + key);
        }
    }

    if (!sawEnd)
        return FailActorIntent("missing actor intent end");
    if (!result.plan.coast && result.plan.actions.empty())
        return FailActorIntent("actor intent contained no actions");
    return result;
}

std::string FormatActorIntentPlan(const ActorIntentPlan& plan)
{
    std::ostringstream out;
    out << "xrsim_actor_intent_v1\n";
    if (plan.coast)
    {
        out << "coast\n";
    }
    else
    {
        if (!plan.goal.empty())
            out << "goal " << plan.goal << "\n";
        if (!plan.stance.empty())
            out << "stance " << plan.stance << "\n";
        if (plan.durationMs != 0)
            out << "duration_ms " << plan.durationMs << "\n";
        for (const ActorAction& action : plan.actions)
        {
            out << "action " << action.verb;
            if (!action.target.empty())
                out << " " << action.target;
            if (!action.arg0.empty())
                out << " " << action.arg0;
            if (!action.arg1.empty())
                out << " " << action.arg1;
            if (action.amount != 0)
                out << " " << action.amount;
            out << "\n";
        }
        for (const std::string& memory : plan.memories)
            out << "memory " << memory << "\n";
    }
    out << "end\n";
    return out.str();
}
} // namespace xrSim
