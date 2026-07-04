#include "stdafx.h"

namespace xray::render::RENDER_NAMESPACE
{
bool xrRender_test_hw()
{
    // Metal is available iff the system provides a default device
    // (any Apple Silicon Mac; Intel Macs with Metal-capable GPUs too).
    MTL::Device* device = MTL::CreateSystemDefaultDevice();
    if (!device)
        return false;
    device->release();
    return true;
}
} // namespace xray::render::RENDER_NAMESPACE
