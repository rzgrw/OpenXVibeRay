<div align="center">
  <h1>OpenXVibeRay</h1>
  <p><strong>A native macOS port of the X-Ray Engine with Metal rendering</strong></p>
  <p>
    Fork of <a href="https://github.com/OpenXRay/xray-16">OpenXRay</a> focused on bringing S.T.A.L.K.E.R. to Apple Silicon with a modern graphics backend.
  </p>
  <p>
    <img src="https://img.shields.io/badge/platform-macOS-000000?logo=apple" alt="macOS" />
    <img src="https://img.shields.io/badge/arch-Apple%20Silicon-333333" alt="Apple Silicon" />
    <img src="https://img.shields.io/badge/renderer-OpenGL%20%E2%9C%93%20%7C%20Metal%20WIP-blue" alt="Renderer" />
    <img src="https://img.shields.io/badge/C%2B%2B-17-blue?logo=cplusplus" alt="C++17" />
  </p>
</div>

---

## What is this?

OpenXVibeRay is a fork of the [OpenXRay](https://github.com/OpenXRay/xray-16) engine — the community-improved X-Ray Engine powering the S.T.A.L.K.E.R. game series. This fork's mission is a **first-class macOS experience** with a native Metal renderer, replacing the deprecated OpenGL path that Apple ships.

**Current status:** The engine builds and runs S.T.A.L.K.E.R.: Call of Chernobyl on macOS Apple Silicon (M4) with the OpenGL renderer. A native Metal renderer is in design.

## Roadmap

| Phase | Status | Description |
|-------|--------|-------------|
| **SP1: macOS Baseline** | Done | Engine builds and runs on macOS with OpenGL |
| **SP2: Metal Renderer** | Designed | Native Metal backend via metal-cpp + SPIRV-Cross shader pipeline |
| **SP3: macOS Polish** | Planned | .app bundle, DMG installer, code signing, Retina support |

## macOS Fixes

Engine changes made to get S.T.A.L.K.E.R. running on macOS ARM64:

- **LuaJIT exception safety** — Disabled `XRAY_EXCEPTIONS` on Darwin. C++ exceptions can't propagate through LuaJIT's ARM64 assembly trampoline; `THROW` macros now compile to `VERIFY` (fatal abort with message) instead of `throw`.
- **ALife simulation stability** — Hardened all `CSafeMapIterator::remove()` call sites with `no_assert=true`. CoC's ALife objects can be in inconsistent state during teleport; missing entries are logged as warnings instead of crashing.
- **LuaJIT build fix** — Replaced hardcoded Xcode.app sysroot with conditional fallback to CommandLineTools SDK.
- **POSIX file permissions** — Fixed `_sopen` macro that created files with 0000 permissions on macOS/Linux/BSD.
- **Sound device fallback** — Graceful handling when saved audio device no longer exists.

## Supported Games

- Call of Chernobyl 1.4.22
- Call of Pripyat 1.6.02
- Clear Sky 1.5.10 (minor bugs possible)

Shadow of Chernobyl is not yet supported by upstream OpenXRay.

## Building on macOS

```bash
# Dependencies
brew install cmake ccache sdl2 lzo libogg libvorbis theora openal-soft jpeg-turbo

# Clone and init submodules
git clone https://github.com/rzgrw/OpenXVibeRay.git
cd OpenXVibeRay
git submodule update --init --recursive

# Build
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_UNITY_BUILD=ON \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build build -j10
```

Binaries output to `bin/arm64/Release/`. See [docs/macos-dev-setup.md](docs/macos-dev-setup.md) for full setup instructions including game data configuration.

## Metal Renderer Design

The Metal renderer will follow the existing GL backend architecture:

- **metal-cpp** — Apple's C++ wrapper for Metal API (no Objective-C++ needed for most code)
- **SPIRV-Cross** — Automatic shader translation: GLSL -> SPIR-V -> Metal Shading Language
- **SDL_Metal_CreateView()** — Native Metal layer setup through SDL2
- **Pipeline State Object cache** — Lazy creation and caching of MTLRenderPipelineState objects

New source directories: `src/Layers/xrRenderMetal/` (hardware abstraction) and `src/Layers/xrRenderPC_Metal/` (render module). Full design spec at [docs/superpowers/specs/2026-03-29-macos-metal-port-design.md](docs/superpowers/specs/2026-03-29-macos-metal-port-design.md).

## Project Structure

```
docs/
  macos-dev-setup.md              # macOS build and run guide
  superpowers/
    specs/                         # Design specifications
    plans/                         # Implementation plans
    CONTINUATION.md                # Roadmap and resume guide
src/
  Layers/
    xrRenderGL/                    # OpenGL backend (current macOS renderer)
    xrRenderPC_GL/                 # OpenGL render module
    xrRenderDX11/                  # DirectX 11 backend (Windows)
    xrRenderPC_R4/                 # DX11 render module
    xrRender/                      # Shared render code (reused by all backends)
    xrRender_R2/                   # Shared render logic
  xrEngine/                        # Core engine (SDL2 windowing, input)
  xrCore/                          # Platform abstractions, file I/O, threading
  xrGame/                          # Game logic, ALife simulation
  xrSound/                         # Audio (OpenAL)
  xr_3da/                          # Main executable entry point
```

## Credits

This project is built on the work of the [OpenXRay team](https://github.com/OpenXRay/xray-16) and the broader S.T.A.L.K.E.R. modding community. See the [upstream contributors list](https://github.com/OpenXRay/xray-16#thanks) for the full history.

Special thanks to **vertver** and **Lnd-stoL** for the original macOS support in OpenXRay that made this fork possible.

---

*This is a fan project. Not affiliated with GSC Game World. Follow the official [EULA](https://www.gsc-game.com/eula/) and [Fan Content Guidelines](https://www.gsc-game.com/guidelines/).*
