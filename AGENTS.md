# OpenXVibeRay — Codex Project Guide

## Project Overview

OpenXVibeRay is a fork of OpenXRay (X-Ray Engine for S.T.A.L.K.E.R.) being rebuilt with AI coding agents. The original engine is **inspiration and foundation, not scripture**. Current direction (plan of record: `docs/superpowers/specs/2026-07-05-engine-roadmap-v2.md`):

- **Renderer:** **DUAL-NATIVE** — native **Metal on macOS** (revived; bring-up Tasks 22–24 = next code milestone) + native **Vulkan on Linux/Windows** (developed on Mac via MoltenVK as a dev vehicle). Plan: `docs/superpowers/specs/2026-07-05-engine-roadmap-v2.md`. GL is the working fallback until both reach parity.
- **AI:** the Zone is simulated by **LLM agents** (xrMind WorldAgents; thin C++ xrSim state store + near-player executor) — spec: `docs/superpowers/specs/2026-07-05-agentic-zone-design.md`. AI comes **after** the renderer.
- **Agent bridge (shipped):** launch with `-agent_bridge`, drive via `tools/agentctl.py` — Codex can run, control, and screenshot the game itself. This is the standard verification harness.

## Architecture

- **Engine core:** C++17, CMake 3.23+, SDL2 for windowing/input, OpenAL for audio
- **Renderers:** OpenGL (working, the current runtime), Metal (Mac renderer, bring-up active, `XRAY_METAL=OFF` until first-frame gate), Vulkan (PC renderer, not yet created), DX11 (Windows legacy, no investment)
- **Scripting:** LuaJIT + luabind for game logic (to be reduced by the AI rehaul)
- **Platform layer:** `src/Common/Platform.hpp` → `PlatformApple.inl` / `PlatformLinux.inl` / `PlatformWindows.inl`

## Key Directories

```
src/Layers/xrRenderPC_GL/    # OpenGL renderer (working baseline)
src/Layers/xrRenderGL/       # GL hardware abstraction
src/Layers/xrRenderMetal/    # Metal HAL — the Mac renderer (bring-up active)
src/Layers/xrRenderPC_Metal/ # Metal module (EXCLUDE_FROM_ALL until first frame)
src/Layers/xrRender/         # Shared render code (USE_DX11/USE_OGL/USE_METAL branches)
src/Layers/xrRender_R2/      # Shared render logic (CRender, phases)
src/xrEngine/                # Core engine, SDL2, input, console, AgentBridge
src/xrCore/                  # Platform abstractions, file I/O, threading, debug
src/xrGame/                  # Game logic, ALife simulation, Lua bindings
src/xr_3da/                  # Main executable, entry_point.cpp
Externals/                   # LuaJIT, ODE, imgui, metal-cpp, glslang, SPIRV-Cross, ...
tools/                       # agentctl.py + bridge soak/acceptance scripts
```

## Build (macOS)

```bash
git submodule update --init --recursive
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_UNITY_BUILD=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build build -j10
```

Binaries go to `bin/arm64/Release/` (not `build/bin/`).

On macOS Tahoe beta, also pass: `-DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk`

`-DXRAY_METAL=ON` builds/links the Metal module (the Mac renderer under bring-up; off by default until its first-frame gate).

## Critical macOS Constraints

- **XRAY_EXCEPTIONS=0 on Darwin** (`src/CMakeLists.txt`). C++ exceptions cannot propagate through LuaJIT's ARM64 trampoline. THROW compiles to VERIFY (fatal abort). Do NOT re-enable. Consequence for all new code: **failures must be values, never throws** (see the two-phase-validation pattern in the specs).
- **`sysctl -n hw.ncpu`** returns trailing space on Tahoe. Use `-j10`.
- **OpenGL 4.1** is the macOS ceiling — the reason for the renderer replacement (native Metal on Mac; Vulkan on PC).

## Testing — use the agent bridge

The engine is self-testable. Launch with `-agent_bridge`, then:

```bash
python3 tools/agentctl.py <gamedir>/appdata/agent_bridge.sock --script tools/bridge_soak.txt
```

Verbs: `hello/cmd/lua/key/mouse/state/shot/bye`. The soak (movement, save/load, UI, quit) is the regression gate before merging engine changes. Screenshots land in `appdata/screenshots/` and are readable by Codex. Run setup details: `docs/macos-dev-setup.md` and the CoC notes in project memory.

## Coding Conventions

- Match the style of the file you're editing (the codebase is not consistent)
- Platform code: `#ifdef XR_PLATFORM_APPLE` / `XR_PLATFORM_WINDOWS` / `XR_PLATFORM_POSIX`
- Renderer branches: `#if defined(USE_DX11)` / `#elif defined(USE_OGL)` / `#elif defined(USE_METAL)` — `USE_VULKAN` joins this pattern; keep GL/DX11 output unchanged when adding branches
- Each renderer module compiles with `RENDER_NAMESPACE=render_gl` / `render_metal` / (next) `render_vulkan`
- Assertions: `VERIFY`, `R_ASSERT`, `THROW` (= VERIFY on macOS)
- Log conventions: `Msg("* info")`, `Msg("! error")`, `Msg("~ warning")`

## Documentation

- `docs/superpowers/specs/2026-07-05-engine-roadmap-v2.md` — **THE plan of record (V1–V5), read first**
- `docs/superpowers/specs/2026-07-05-vulkan-renderer-roadmap.md` — current renderer plan (V-R0…V-R3)
- `docs/superpowers/specs/2026-07-05-agentic-zone-design.md` — current AI plan (xrMind/xrSim)
- Parked/superseded (banners inside say so): old 07-04 roadmap, zone-simulation (deterministic parts), seamless streaming, `CONTINUATION.md`. The SP2 Metal plan is **REVIVED** (Tasks 22–24 active).
- `docs/macos-dev-setup.md` — build/run + agent bridge usage

## Current State (2026-07-06)

> **Start here: [`docs/HANDOVER.md`](docs/HANDOVER.md)** — full handover (renderer status, build/run/test, Metal resume guide, AI phase).

- **Direction (2026-07-06):** **OpenGL is the working runtime; the active focus is the AI rehaul built on top of GL.** The native-renderer rewrite is paused.
- **Engine stabilization:** complete — exit hangs, signal safety, resolution handling, GL error surfacing all fixed; bridge soak green.
- **Agent bridge:** shipped and proven (the AI-work verification harness).
- **Metal backend (Mac renderer):** M-R0 done (menu renders — PR #9); M-R1 (in-game) parked mid-flight on `feat/metal-mr1-ingame` — resume guide in the handover.
- **Vulkan renderer (PC):** not started.
- **AI (agentic Zone):** the next focus — spec `docs/superpowers/specs/2026-07-05-agentic-zone-design.md`, developed on the GL runtime.
