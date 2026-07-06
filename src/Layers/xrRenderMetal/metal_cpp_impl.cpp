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

// Keep ObjC's 1-byte BOOL out of the engine's namespace (see stdafx.h —
// the engine's serialized structs require the 4-byte int32_t BOOL).
#define BOOL objc_darwin_BOOL
#include <Foundation/Foundation.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <Metal/Metal.hpp>
#undef BOOL
