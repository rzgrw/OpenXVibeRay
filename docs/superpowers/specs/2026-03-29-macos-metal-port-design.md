# Viberay macOS Native Port with Metal Renderer — Design Spec

**Date:** 2026-03-29
**Status:** Draft
**Author:** Collaborative design (user + AI)

## Overview

Port Viberay (OpenXRay fork) to run natively on macOS with a Metal renderer, proper .app bundle, and DMG distribution. This is Viberay's first major update as a fork.

**Target platform:** macOS on Apple Silicon (arm64)
**Renderer:** Native Metal via metal-cpp + SPIRV-Cross shader pipeline
**Distribution:** .app bundle in a DMG installer, code-signed and notarized

## Project Decomposition

Three sequential sub-projects, each building on the previous:

| Sub-project | Goal | Success Criteria |
|---|---|---|
| **SP1: macOS Baseline** | Verify engine builds and runs on macOS with OpenGL | Walk around a S.T.A.L.K.E.R. level, audio + input working |
| **SP2: Metal Renderer** | Replace OpenGL with native Metal backend | Same gameplay as SP1, rendered via Metal, no visual regressions |
| **SP3: macOS Polish** | .app bundle, DMG, signing, macOS-native UX | Distributable DMG that launches cleanly on a fresh Mac |

---

## SP1: macOS Baseline

### Goal

Verify the existing macOS support actually works end-to-end. The engine has CI builds for macOS arm64 and x86_64, platform abstractions in `src/Common/PlatformApple.inl`, and SDL2/OpenAL for windowing/audio. SP1 validates this.

### Tasks

1. **Install dependencies** via Homebrew:
   ```
   brew install cmake ccache sdl2 lzo libogg libvorbis theora openal-soft jpeg-turbo
   ```

2. **Build** with CMake:
   ```
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build -j$(sysctl -n hw.ncpu)
   ```

3. **Set up game data** — Copy Call of Pripyat or Call of Chernobyl game files to the expected location.

4. **Launch and verify:**
   - Window opens via SDL2
   - Main menu renders (OpenGL)
   - Audio plays (OpenAL)
   - Can load a level and walk around
   - Input is responsive (keyboard + mouse via SDL2)

5. **Document issues** — Any bugs found become blockers for SP2.

### Existing Platform Support

| Component | Status | Implementation |
|---|---|---|
| Platform detection | Done | `src/Common/Platform.hpp` — `XR_PLATFORM_APPLE` |
| Type definitions | Done | `src/Common/PlatformApple.inl` |
| Build system | Done | `cmake/XRay.Compiler.GNULike.cmake` — Apple-specific flags |
| Windowing | Done | SDL2 (`src/xrEngine/device.h`) |
| Input | Done | SDL2 (`src/xrEngine/xr_input.h`) |
| Audio | Done | OpenAL (`src/xrSound/`) |
| Rendering (GL) | Done | `src/Layers/xrRenderPC_GL/` + `src/Layers/xrRenderGL/` |
| Threading | Done | std::thread + POSIX (`src/xrCore/Threading/`) |
| File I/O | Done | POSIX abstractions |
| CI | Done | GitHub Actions — arm64 + x86_64 builds |

---

## SP2: Metal Renderer

### Architecture Overview

Create a new Metal renderer following the exact architecture of the existing GL backend. The engine's renderer registration system (`RendererModule` in `src/xrEngine/EngineAPI.h`) allows new backends to plug in cleanly.

### New Source Directories

```
src/Layers/
├── xrRenderMetal/              # Metal hardware abstraction (NEW)
│   ├── metalHW.h               # CHW class — MTLDevice, command queue, layer
│   ├── metalHW.cpp
│   ├── metalHW_Apple.mm        # Thin ObjC++ bridge for CAMetalLayer setup
│   ├── metalState.h            # Pipeline state object cache
│   ├── metalState.cpp
│   ├── metalConstantBuffer.h   # MTLBuffer-backed constant buffers
│   ├── metalConstantBuffer.cpp
│   ├── metalr_constants_cache.h # Shader constant caching (matches GL pattern)
│   ├── metalTexture.h          # MTLTexture creation, binding, sampling
│   ├── metalTexture.cpp
│   ├── metalRenderTarget.h     # MTLRenderPassDescriptor management
│   ├── metalRenderTarget.cpp
│   ├── metalShaderCompiler.h   # SPIRV-Cross MSL pipeline
│   ├── metalShaderCompiler.cpp
│   ├── metalVertexInput.h      # MTLVertexDescriptor from X-Ray vertex declarations
│   ├── metalVertexInput.cpp
│   └── CMakeLists.txt
│
├── xrRenderPC_Metal/           # Metal render module (NEW)
│   ├── xrRender_Metal.h        # RMetalRendererModule : RendererModule
│   ├── xrRender_Metal.cpp
│   ├── metal_rendertarget.h    # G-buffer, shadow maps, post-FX targets
│   ├── metal_rendertarget.cpp
│   ├── metal_rendertarget_phase_accumulator.cpp
│   ├── metal_rendertarget_phase_bloom.cpp
│   ├── metal_rendertarget_phase_combine.cpp
│   ├── metal_rendertarget_phase_ssao.cpp
│   ├── metal_shaders.cpp       # Shader loading via SPIRV-Cross
│   ├── entry_point.cpp         # GetRendererModule() factory
│   └── CMakeLists.txt
```

### Hardware Abstraction Layer (`metalHW`)

The `CHW` class encapsulates Metal device and presentation:

```cpp
class CHW
{
public:
    // Device
    MTL::Device*        m_device;           // GPU device
    MTL::CommandQueue*  m_commandQueue;     // Command submission queue

    // Presentation
    CA::MetalLayer*     m_metalLayer;       // Backed by SDL window
    MTL::Drawable*      m_currentDrawable;  // Current frame's drawable

    // Per-frame
    MTL::CommandBuffer* m_commandBuffer;    // Current frame's command buffer

    // Capabilities
    struct Caps {
        bool msaa_supported;
        u32 max_texture_size;
        bool depth32_supported;
        MTL::PixelFormat preferred_depth_format;
    } caps;

    void CreateDevice(SDL_Window* sdlWindow);
    void DestroyDevice();
    void Reset(SDL_Window* sdlWindow);
    void Present();

    MTL::RenderCommandEncoder* BeginRenderPass(MTL::RenderPassDescriptor* desc);
    void EndRenderPass(MTL::RenderCommandEncoder* encoder);
};
```

**CAMetalLayer setup** requires a thin Objective-C++ bridge (`metalHW_Apple.mm`):
- SDL2 provides access to the NSWindow/NSView via `SDL_GetWindowWMInfo`
- Create CAMetalLayer and attach to view's layer
- This is the only .mm file needed; everything else uses metal-cpp (C++ headers)

### Shader Pipeline

#### Build-time compilation (primary path)

```
GLSL source files (res/gamedata/shaders/gl/*.glsl)
    → glslang (GLSL → SPIR-V bytecode)
    → SPIRV-Cross (SPIR-V → Metal Shading Language)
    → Metal compiler (MSL → .metallib archive)
```

A CMake custom command runs this pipeline during build. The `.metallib` files are bundled with the app.

#### Runtime compilation (mod support fallback)

For modded shaders not in the pre-compiled set:
1. Load GLSL source at runtime
2. Use bundled SPIRV-Cross library to translate GLSL → SPIR-V → MSL
3. Compile MSL to `MTLFunction` via `MTLDevice::newLibrary(source:)`
4. Cache the compiled library on disk for next launch

#### Shader resource binding

X-Ray uses named constants (uniforms). The shader compiler extracts:
- Vertex attributes → `MTLVertexDescriptor`
- Fragment/vertex uniforms → `MTLBuffer` offsets
- Texture samplers → `setFragmentTexture` / `setVertexTexture` indices

The `metalr_constants_cache.h` maps X-Ray constant names to Metal buffer offsets, following the same pattern as `glr_constants_cache.h`.

### Backend Integration

#### CBackend modifications (`src/Layers/xrRender/R_Backend.h`)

Add `#elif defined(USE_METAL)` branches alongside existing `USE_DX11` and `USE_OGL`:

```cpp
// Render targets
#if defined(USE_DX11)
    ID3DRenderTargetView*   pRT[4];
    ID3DDepthStencilView*   pZB;
#elif defined(USE_OGL)
    GLuint pFB, pRT[4], pZB;
#elif defined(USE_METAL)
    MTL::Texture*               pRT[4];
    MTL::Texture*               pZB;
    MTL::RenderCommandEncoder*  pEncoder;
#endif

// Shader binding
#if defined(USE_METAL)
    MTL::RenderPipelineState*   pCurrentPSO;
    MTL::DepthStencilState*     pCurrentDSS;
#endif
```

#### Pipeline State Object (PSO) Cache

Metal requires pre-compiled pipeline state objects (unlike GL's state machine). The `metalState` module:

- Hashes: vertex shader + fragment shader + vertex layout + blend state + pixel format
- Maintains `std::unordered_map<PSOHash, MTL::RenderPipelineState*>`
- Creates PSOs lazily on first use, caches for reuse
- Typical scene uses ~50-200 unique PSOs

#### Render Pass Mapping

X-Ray's render phases map to Metal render passes:

| X-Ray Phase | Metal Render Pass |
|---|---|
| G-buffer fill | Write to position/normal/color/depth textures |
| Shadow map | Depth-only pass to shadow atlas |
| Light accumulation | Read G-buffer, write to accumulation buffer |
| Volumetric/fog | Additional accumulation passes |
| Post-processing (bloom, SSAO) | Successive passes reading/writing RT textures |
| Final combine | Composite to screen drawable |

Each phase becomes a `MTLRenderPassDescriptor` with load/store actions configured for optimal tile memory usage on Apple Silicon.

### Module Registration

In `src/xr_3da/entry_point.cpp`:

```cpp
std::array<RendererModule*, 3> s_render_modules =
{
#ifdef XR_PLATFORM_WINDOWS
    xray::render::render_r4::GetRendererModule(),   // DX11
#endif
#ifdef XR_PLATFORM_APPLE
    xray::render::render_metal::GetRendererModule(), // Metal
#endif
    xray::render::render_gl::GetRendererModule(),    // OpenGL (fallback)
};
```

Metal is preferred over GL on macOS. GL remains as fallback.

### Reused Code (unchanged)

All shared rendering logic works with any backend:

- `D3DXRenderBase` — base render implementation (~2K LOC)
- `CRender` in `xrRender_R2/` — scene rendering, phases, lights (~15K LOC)
- `ResourceManager` — shader/texture caching (needs Metal additions for resource types)
- 100+ blenders in `xrRender/blenders/` — material definitions
- Particle system, wallmark system, detail manager
- Scene graph, visibility, occlusion queries

### Dependencies

New dependencies for SP2:

| Dependency | Purpose | How obtained |
|---|---|---|
| metal-cpp | C++ Metal API wrapper | Header-only, bundled in `Externals/` |
| SPIRV-Cross | SPIR-V → MSL translation | Bundled in `Externals/` or Homebrew |
| glslang | GLSL → SPIR-V compilation | Bundled in `Externals/` or Homebrew |
| Metal framework | macOS system framework | System (Xcode SDK) |
| QuartzCore framework | CAMetalLayer | System (Xcode SDK) |

### Testing Strategy

1. **Visual comparison:** Render the same scene with GL and Metal, screenshot and diff
2. **Render feature parity checklist:**
   - [ ] G-buffer rendering (position, normal, color)
   - [ ] Shadow mapping (cascaded sun shadows)
   - [ ] Deferred lighting (point, spot, sun)
   - [ ] Bloom post-processing
   - [ ] SSAO
   - [ ] Particle effects
   - [ ] Fog/volumetrics
   - [ ] HUD/UI rendering
3. **Performance:** Metal should be faster than GL on Apple Silicon (target: stable 60fps at 1080p)
4. **Stability:** No crashes during 30-minute play sessions

---

## SP3: macOS Polish

### Application Bundle

Create a proper macOS `.app` bundle via CMake:

```
Viberay.app/
├── Contents/
│   ├── Info.plist
│   ├── MacOS/
│   │   └── viberay
│   ├── Resources/
│   │   ├── viberay.icns
│   │   └── shaders/          # Pre-compiled .metallib files
│   ├── Frameworks/
│   │   ├── libSDL2.dylib
│   │   ├── libopenal.dylib
│   │   └── ...               # Other bundled dylibs
│   └── _CodeSignature/
```

**CMake configuration:**
- Set `MACOSX_BUNDLE TRUE` on the executable target
- Use `INSTALL_RPATH @executable_path/../Frameworks`
- `fixup_bundle()` or manual `install_name_tool` for dylib paths
- `Info.plist.in` template with `@PROJECT_VERSION@` substitution

### DMG Installer

- Use `create-dmg` tool or `hdiutil` in CI
- Drag-and-drop layout: Viberay.app → Applications alias
- Background image with project branding
- CI artifact: `Viberay-<version>-macOS-arm64.dmg`

### Code Signing & Notarization

- Sign with Apple Developer ID certificate
- Notarize via `xcrun notarytool`
- Staple notarization ticket to DMG
- CI secrets for certificate (optional for open-source — unsigned dev builds are fine)

### macOS-Native UX

- **Menu bar:** SDL2 provides basic menu bar; verify Cmd+Q works for quit
- **Retina/HiDPI:** Metal's drawable size auto-scales; verify UI renders at correct size
- **Dock icon:** Provided by .app bundle's icns file
- **File dialogs:** Use native NSOpenPanel for game data directory selection (thin ObjC++ bridge)
- **Crash reports:** Integrate with macOS crash reporter via exception handler

---

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| SPIRV-Cross produces incorrect MSL for some shaders | Medium | High | Test all shaders early; hand-fix edge cases |
| Metal PSO compilation stalls cause hitching | Medium | Medium | Pre-warm PSO cache during loading screens |
| Apple Silicon GPU quirks (tile-based rendering) | Low | Medium | Follow Apple's Metal best practices for TBDR |
| Game data path differences vs Windows | Low | Low | Already handled by POSIX abstractions |
| OpenGL baseline has unfound bugs | Medium | Low | SP1 catches these before Metal work begins |

## Non-Goals

- Vulkan renderer (not needed for macOS, could be future work for Linux)
- iOS/iPadOS support (different input model, would need separate design)
- Windows Metal support (doesn't exist)
- Rewriting the engine's scene graph or game logic
- Supporting Shadow of Chernobyl (upstream doesn't support it either)
