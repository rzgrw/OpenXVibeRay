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

bool HasNoTrailingTokens(std::istringstream& record)
{
    return (record >> std::ws).peek() == EOF;
}

bool ReadSingleToken(std::istringstream& record, std::string& value)
{
    if (!(record >> value))
        return false;
    return HasNoTrailingTokens(record);
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

    bool seenGoal = false;
    bool seenStance = false;
    bool seenDuration = false;
    bool seenAction = false;
    bool seenMemory = false;

    bool sawEnd = false;
    while (std::getline(input, line))
    {
        if (line.empty())
            continue;

        if (result.plan.coast && line != "coast" && line != "end")
            return FailActorIntent("coast cannot be mixed with intent metadata");

        if (line == "end")
        {
            sawEnd = true;
            break;
        }

        if (line == "coast")
        {
            if (seenAction || seenGoal || seenStance || seenDuration || seenMemory)
                return FailActorIntent("coast cannot be mixed with intent metadata");
            result.plan.coast = true;
            continue;
        }

        std::istringstream record(line);
        std::string key;
        record >> key;
        if (key == "goal")
        {
            if (!ReadSingleToken(record, result.plan.goal))
                return FailActorIntent("invalid goal record");
            seenGoal = true;
        }
        else if (key == "stance")
        {
            if (!ReadSingleToken(record, result.plan.stance))
                return FailActorIntent("invalid stance record");
            seenStance = true;
        }
        else if (key == "duration_ms")
        {
            std::string value;
            if (!ReadSingleToken(record, value) || !ParseUint32Token(value, result.plan.durationMs))
                return FailActorIntent("invalid duration_ms record");
            seenDuration = true;
        }
        else if (key == "action")
        {
            ActorAction action;
            std::string amountText;
            if (!(record >> action.verb >> action.target))
                return FailActorIntent("invalid action record");

            if (record >> action.arg0)
            {
                if (record >> action.arg1)
                {
                    if (record >> amountText)
                    {
                        if (!ParseInt32Token(amountText, action.amount) || !HasNoTrailingTokens(record))
                            return FailActorIntent("invalid action amount");
                    }
                    else if (!HasNoTrailingTokens(record))
                    {
                        return FailActorIntent("invalid action amount");
                    }
                }
                else if (!HasNoTrailingTokens(record))
                {
                    return FailActorIntent("invalid action amount");
                }
            }
            else if (!HasNoTrailingTokens(record))
            {
                return FailActorIntent("invalid action amount");
            }
            else
            {
                action.arg0.clear();
                action.arg1.clear();
            }

            result.plan.actions.push_back(action);
            seenAction = true;
        }
        else if (key == "memory")
        {
            std::string memory;
            if (!ReadSingleToken(record, memory))
                return FailActorIntent("invalid memory record");
            result.plan.memories.push_back(memory);
            seenMemory = true;
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
