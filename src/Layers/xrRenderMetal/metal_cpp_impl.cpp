/*
 * metal-cpp single-TU implementation.
 *
 * metal-cpp headers declare ObjC selector/class symbols (NS::Private::Selector,
 * CA::Private::Selector, MTL::Private::Class, ...) that are DEFINED only in the
 * translation unit that sets the *_PRIVATE_IMPLEMENTATION macros before
 * including the headers. Exactly one such TU may exist per binary.
 *
 * This file must be excluded from unity builds and precompiled headers —
 * both would inject <Metal/Metal.hpp> (via stdafx.h) before our defines,
 * and the include guards would then suppress the implementation.
 */
#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION

#include <Foundation/Foundation.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <Metal/Metal.hpp>
