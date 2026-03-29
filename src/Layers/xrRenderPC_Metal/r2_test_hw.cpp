#include "stdafx.h"

bool xrRender_test_hw()
{
    // Check if Metal is available.
    // For now, just return true on Apple platforms.
    // Real check via MTL::CreateSystemDefaultDevice() will come in Task 9.
    return true;
}
