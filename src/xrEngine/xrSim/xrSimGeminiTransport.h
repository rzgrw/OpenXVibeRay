#pragma once

#include "xrSim/xrSimAgentProvider.h"

#include <memory>
#include <string>

namespace xrSim
{
std::unique_ptr<IGeminiTransport> CreateGeminiHttpTransport();
bool IsGeminiHttpTransportAvailable();
std::string DescribeGeminiHttpTransport();
} // namespace xrSim
