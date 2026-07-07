#pragma once

#include "xrSim/xrSimAgentProvider.h"

#include <memory>
#include <string>

namespace xrSim
{
std::unique_ptr<IAnthropicTransport> CreateAnthropicHttpTransport();
std::unique_ptr<IAgentProvider> CreateLiveAgentProvider(const AgentProviderConfig& config);
bool IsAnthropicHttpTransportAvailable();
std::string DescribeAnthropicHttpTransport();
} // namespace xrSim
