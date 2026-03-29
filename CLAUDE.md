# OpenXVibeRay — Claude Code Project Guide

## Project Overview

OpenXVibeRay is a fork of OpenXRay (X-Ray Engine for S.T.A.L.K.E.R.) being systematically refactored and improved with AI coding agents. The first major milestone was a native macOS port; the next is a Metal renderer.

## Architecture

- **Engine core:** C++17, CMake 3.23+, SDL2 for windowing/input, OpenAL for audio
- **Renderers:** OpenGL (cross-platform), DirectX 11 (Windows). Metal renderer in design.
- **Scripting:** LuaJIT + luabind for game logic. CoC/Anomaly mods use Lua extensively.
- **Platform layer:** `src/Common/Platform.hpp` detects platform, includes `PlatformApple.inl` / `PlatformLinux.inl` / `PlatformWindows.inl`

## Key Directories

```
src/Layers/xrRenderPC_GL/   # OpenGL renderer (macOS template for Metal)
src/Layers/xrRenderGL/       # GL hardware abstraction
src/Layers/xrRenderDX11/     # DX11 hardware abstraction (Windows)
src/Layers/xrRender/         # Shared render code (all backends use this)
src/Layers/xrRender_R2/      # Shared render logic (CRender, phases)
src/xrEngine/                # Core engine, SDL2, input, console, renderer API
src/xrCore/                  # Platform abstractions, file I/O, threading, debug
src/xrGame/                  # Game logic, ALife simulation, Lua script bindings
src/xrSound/                 # Audio via OpenAL
src/xr_3da/                  # Main executable, entry_point.cpp
Externals/                   # Third-party: LuaJIT, ODE, OPCODE, imgui, etc.
```

## Build (macOS)

```bash
git submodule update --init --recursive
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_UNITY_BUILD=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build build -j10
```

Binaries go to `bin/arm64/Release/` (not `build/bin/`).

On macOS Tahoe beta, also pass: `-DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk`

## Critical macOS Constraints

- **XRAY_EXCEPTIONS=0 on Darwin** (`src/CMakeLists.txt`). C++ exceptions cannot propagate through LuaJIT's ARM64 assembly trampoline. THROW macros compile to VERIFY (fatal abort) instead of throw. Do NOT re-enable.
- **`sysctl -n hw.ncpu`** returns trailing space on Tahoe. Use `-j10` not `--parallel $(sysctl -n hw.ncpu)`.
- **OpenGL 4.1** is the max on macOS. Shadow/lighting glitches are expected; this is why we need the Metal renderer.

## Testing

No automated test suite exists for this engine. Testing is manual:
1. Build the engine
2. Launch with Call of Chernobyl or Call of Pripyat game data
3. Load into a level, walk around, verify no crashes
4. Check console output for errors

## Coding Conventions

- Follow existing code style in each file (the codebase is not consistent — match the file you're editing)
- Use `#ifdef XR_PLATFORM_APPLE` / `XR_PLATFORM_WINDOWS` / `XR_PLATFORM_POSIX` for platform-specific code
- Renderer backends use `#if defined(USE_DX11)` / `#elif defined(USE_OGL)` / `#elif defined(USE_METAL)` (future)
- Each renderer compiles with `RENDER_NAMESPACE=render_gl` (or `render_r4`, future `render_metal`)
- X-Ray assertion macros: `VERIFY`, `R_ASSERT`, `THROW` (THROW becomes VERIFY on macOS)
- Log conventions: `Msg("* info")`, `Msg("! error")`, `Msg("~ warning")`

## Documentation

- `docs/superpowers/specs/` — Design specifications
- `docs/superpowers/plans/` — Implementation plans
- `docs/superpowers/CONTINUATION.md` — Full roadmap and resume guide for SP2
- `docs/macos-dev-setup.md` — macOS build and run guide

## Current State

- **SP1 (macOS Baseline):** Complete. Tagged `sp1-macos-baseline`.
- **SP2 (Metal Renderer):** Design spec complete. Implementation not started.
- **SP3 (macOS Polish):** Planned (app bundle, DMG, signing).

When resuming Metal renderer work, read `docs/superpowers/CONTINUATION.md` first.
