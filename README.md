<div align="center">
  <h1>OpenXVibeRay</h1>
  <p><strong>X-Ray Engine, refactored and improved with AI coding agents</strong></p>
  <p>
    Fork of <a href="https://github.com/OpenXRay/xray-16">OpenXRay</a> — proving that a legacy C++ game engine can be systematically fixed, ported, and modernized through human-AI collaboration.
  </p>
  <p>
    <img src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-blue" alt="Platforms" />
    <img src="https://img.shields.io/badge/macOS-Apple%20Silicon%20%E2%9C%93-000000?logo=apple" alt="Apple Silicon" />
    <img src="https://img.shields.io/badge/renderer-OpenGL%20%E2%9C%93%20%7C%20Metal%20WIP-blue" alt="Renderer" />
    <img src="https://img.shields.io/badge/built%20with-Claude%20Code-blueviolet" alt="Built with Claude Code" />
  </p>
</div>

---

## The Concept

The X-Ray Engine is a 20-year-old C++ codebase — hundreds of thousands of lines, deep platform assumptions, tangled subsystems, and bugs that have survived since 2004. The community has kept it alive, but major refactoring has always been limited by the sheer scale of the work.

**OpenXVibeRay is a proof of concept:** What happens when you point modern AI coding agents at a real, complex, legacy engine? Not a toy project or a greenfield app — a battle-tested game engine with real users and real bugs.

Every commit in this repo was made through human-AI collaboration using [Claude Code](https://claude.ai/code). The AI investigates crashes, traces call stacks through dylib boundaries, diagnoses architectural issues (like C++ exceptions failing to propagate through LuaJIT's ARM64 assembly trampolines), proposes fixes, writes code, and gets reviewed — all in the same workflow a human engineer would follow, just faster.

**This isn't about replacing developers. It's about what becomes possible when an AI agent can hold a 1M-token context window full of engine code and systematically work through problems that would take a human days to trace.**

## What's Been Done

### macOS Native Port (SP1 - Complete)

The first major milestone: getting S.T.A.L.K.E.R. running on macOS Apple Silicon. This required diagnosing and fixing real engine bugs, not just flipping build flags:

| Fix | What was broken | Root cause |
|-----|----------------|------------|
| **XRAY_EXCEPTIONS=0 on Darwin** | Any Lua script error crashed the process | C++ exceptions can't unwind through LuaJIT's ARM64 assembly trampoline (`lj_BC_FUNCC`) — `std::terminate` instead of propagation |
| **ALife registry hardening** | Random crashes during gameplay | `CSafeMapIterator::remove()` throwing through LuaJIT boundary; CoC objects in inconsistent state during teleport |
| **LuaJIT sysroot fix** | Build failure on CommandLineTools-only Macs | Hardcoded `/Applications/Xcode.app/` path in LuaJIT's CMakeLists |
| **POSIX `_sopen` macro** | Files created with 0000 permissions | Windows share-mode arg landing in POSIX `open()` mode slot |
| **Sound device fallback** | "Invalid syntax" error on device change | No graceful fallback when saved audio device no longer exists |

These aren't surface-level patches — the XRAY_EXCEPTIONS fix required understanding the interaction between X-Ray's exception macros, luabind's error handling, LuaJIT's C function trampoline, and ARM64 unwind tables.

### What's Next

| Phase | Status | Description |
|-------|--------|-------------|
| **SP1: macOS Baseline** | Done | Engine builds and runs on macOS Apple Silicon with OpenGL |
| **SP2: Metal Renderer** | Designed | Native Metal backend — metal-cpp, SPIRV-Cross shader pipeline, PSO cache |
| **SP3: macOS Polish** | Planned | .app bundle, DMG installer, code signing, Retina support |
| **Engine Improvements** | Ongoing | Fix bugs, improve stability, modernize code across all platforms |

The Metal renderer design spec is complete ([read it here](docs/superpowers/specs/2026-03-29-macos-metal-port-design.md)). The approach: new `xrRenderMetal/` backend following the existing GL architecture, with SPIRV-Cross for automatic GLSL-to-MSL shader translation.

But macOS is just the starting point. The long-term goal is proving that AI agents can systematically improve any part of this engine — rendering, physics, AI, networking, tooling — at a pace that wasn't previously feasible.

## Supported Games

- Call of Chernobyl 1.4.22
- Call of Pripyat 1.6.02
- Clear Sky 1.5.10 (minor bugs possible)

## Building

### macOS (Apple Silicon / Intel)

```bash
brew install cmake ccache sdl2 lzo libogg libvorbis theora openal-soft jpeg-turbo

git clone https://github.com/rzgrw/OpenXVibeRay.git
cd OpenXVibeRay
git submodule update --init --recursive

cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_UNITY_BUILD=ON \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build build -j10
```

Binaries go to `bin/arm64/Release/`. Full setup guide: [docs/macos-dev-setup.md](docs/macos-dev-setup.md)

### Windows / Linux

Follow the upstream [OpenXRay build instructions](https://github.com/OpenXRay/xray-16/wiki). All upstream platforms remain supported.

## Project Structure

```
src/
  Layers/
    xrRenderGL/         # OpenGL backend (current macOS renderer)
    xrRenderDX11/       # DirectX 11 backend (Windows)
    xrRender/           # Shared render code (all backends)
  xrEngine/             # Core engine — SDL2 windowing, input
  xrCore/               # Platform abstractions, file I/O, threading
  xrGame/               # Game logic, ALife simulation, Lua scripting
  xrSound/              # Audio (OpenAL)
  xr_3da/               # Main executable entry point
docs/
  macos-dev-setup.md    # macOS build and run guide
  superpowers/
    specs/              # Design specifications (Metal renderer, etc.)
    plans/              # Implementation plans
    CONTINUATION.md     # Full roadmap and resume guide
```

## Credits

Built on the work of the [OpenXRay team](https://github.com/OpenXRay/xray-16) and the S.T.A.L.K.E.R. modding community. Special thanks to **vertver** and **Lnd-stoL** for the original macOS support in OpenXRay.

AI-assisted development powered by [Claude Code](https://claude.ai/code) (Anthropic).

---

*Fan project. Not affiliated with GSC Game World. Follow the official [EULA](https://www.gsc-game.com/eula/) and [Fan Content Guidelines](https://www.gsc-game.com/guidelines/).*
