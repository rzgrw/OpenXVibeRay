# OpenXVibeRay — Handover

**Last updated:** 2026-07-06
**Written for:** a fresh coding agent (Codex) or engineer picking this repo up with zero prior context.

---

## 0. TL;DR — current direction

Two things are true right now:

1. **OpenGL is the working runtime.** The game boots, renders, and is fully playable on the GL backend (`renderer renderer_r3`). This is the baseline for everything below.
2. **The active focus is the AI rehaul, developed on top of GL.** The renderer rewrite (native Metal / Vulkan) is **paused**. We are not blocked on graphics — GL is good enough to build the AI-driven Zone on top of. Metal M-R0 (menu) shipped; Metal M-R1 (in-game) is parked mid-flight with a clean resume guide (§4).

So: **build GL, run GL, build the AI on GL.** Pick up Metal later from §4 if/when someone wants to resume the native-renderer track.

Plan of record for the overall vision: [`docs/superpowers/specs/2026-07-05-engine-roadmap-v2.md`](superpowers/specs/2026-07-05-engine-roadmap-v2.md).
AI design spec (the active work): [`docs/superpowers/specs/2026-07-05-agentic-zone-design.md`](superpowers/specs/2026-07-05-agentic-zone-design.md).

---

## 1. Repo state & branches

| Branch | State | Notes |
|---|---|---|
| `dev` | main line, GL working; **M-R0 merged** (PR #9) + this handover | base for the AI work |
| `feat/metal-mr0-first-frame` | **merged to dev** via PR [#9](https://github.com/rzgrw/OpenXVibeRay/pull/9) | Metal renders the CoC main menu. GL/DX11 untouched. |
| `feat/metal-mr1-ingame` | pushed WIP (`caa2e4953`), **not merged** | Metal deferred-pipeline foundation; in-game scene not yet rendering. Resume guide in §4. Branched off the pre-merge M-R0 tip — rebase onto current `dev` when resuming. |

All Metal changes are `USE_METAL`/`SM_METAL`-gated, so **GL and DX11 output is unchanged** on every branch.

---

## 2. Build

macOS (Apple Silicon):

```bash
git submodule update --init --recursive
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_UNITY_BUILD=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build build -j10
```

- Binaries land in `bin/arm64/Release/` (NOT `build/bin/`).
- On macOS Tahoe beta also pass `-DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk`.
- `sysctl -n hw.ncpu` returns a trailing space on Tahoe — use a literal `-j10`.
- **Build times:** a Metal-renderer rebuild is >2 min; give `cmake --build` a timeout ≥ 7 min. (The default 2-min shell timeout will kill it mid-link.)
- **GL is the default build.** `-DXRAY_METAL=ON` additionally builds/links the Metal module; leave it off for GL/AI work.

Hard constraint — **`XRAY_EXCEPTIONS=0` on Darwin** (`src/CMakeLists.txt`): C++ exceptions cannot cross LuaJIT's ARM64 trampoline, so `THROW` compiles to `VERIFY` (a fatal abort). **All new code must return failures as values, never throw.** SPIRV-Cross/glslang exceptions are OK only because they're caught entirely below any Lua frame.

---

## 3. Run & test (the agent bridge is the harness)

The engine is self-testable over a Unix socket. This is how you (an agent) drive and observe the game.

### 3.1 Game data location
- Base install: `/Users/rz/stalkercoc` (full CoC, ~8 GB).
- **Active run dir:** `/Users/rz/Downloads/S.T.A.L.K.E.R. - Call of Chernobyl` (an update package with symlinks into the base install; this is where the game actually runs from — `user.ltx`, saves, logs, screenshots, and the loaded shader copy all live under its `appdata/` and `gamedata/`).

### 3.2 Launch
```bash
cd "/Users/rz/Downloads/S.T.A.L.K.E.R. - Call of Chernobyl"
# menu only:
/Users/rz/OpenXVibeRay/bin/arm64/Release/xr_3da -agent_bridge
# load save "1" (l05_bar) straight in:
/Users/rz/OpenXVibeRay/bin/arm64/Release/xr_3da -agent_bridge -start 'server(1/single/alife/load)' 'client(localhost)'
```
Run it **backgrounded / detached** (it's a long-lived GUI process). `nohup ... &` in a plain shell can get SIGHUP'd when the tool wrapper exits — prefer your harness's own background mechanism.

### 3.3 Drive it
```bash
SOCK="appdata/agent_bridge.sock"
python3 /Users/rz/OpenXVibeRay/tools/agentctl.py "$SOCK" state          # scene/fps/level/pos/paused
python3 /Users/rz/OpenXVibeRay/tools/agentctl.py "$SOCK" shot myshot    # -> appdata/screenshots/myshot.jpg (readable by the agent)
python3 /Users/rz/OpenXVibeRay/tools/agentctl.py "$SOCK" key SPACE tap  # dismiss the "press any key" load screen
python3 /Users/rz/OpenXVibeRay/tools/agentctl.py "$SOCK" cmd 'main_menu off'
python3 /Users/rz/OpenXVibeRay/tools/agentctl.py "$SOCK" cmd 'flush'    # force the log to disk
```
Verbs: `hello/cmd/lua/key/mouse/state/shot/bye`. The soak regression gate: `tools/agentctl.py <sock> --script tools/bridge_soak.txt`.
Log: `appdata/logs/openxray_radik zagirov.log` (truncate before a run to isolate output).

### 3.5 GL stability gate

Before AI work, keep `renderer_r3` green with the GL macOS soak wrapper:

```bash
python3 /Users/rz/OpenXVibeRay/tools/gl_macos_soak.py \
  --scenario /Users/rz/OpenXVibeRay/tools/gl_load_play_save_load.txt \
  --repeat 5 \
  --artifacts /Users/rz/OpenXVibeRay/artifacts/gl_macos_soak/repeat_5
```

This launches the game from the CoC run directory, drives the bridge, samples FPS/frame progress and RSS, validates screenshots/logs/exit, and writes `summary.json` plus `report.txt`.

### 3.4 Two gotchas that will waste your time
- **Do NOT use `-nosound`.** It crashes CoC's `sound_theme.script` (`nil played_id`). To silence audio, set in `appdata/user.ltx` (while the game is stopped): `snd_volume_eff 0.` and `snd_volume_music 0.` — keeps the sound subsystem alive so scripts work.
- **Renderer select** is `renderer` in `appdata/user.ltx`: `renderer_r3` = GL (**current default**), `renderer_metal` = Metal. Change while the game is stopped.

---

## 4. Metal M-R1 resume guide (parked — read only if resuming the renderer)

**Status:** menu renders perfectly on Metal; in-game the **gbuffer geometry pass works** (rt_Position holds the full scene) but the **deferred lighting/combine chain produces a magenta frame**. Foundation is committed on `feat/metal-mr1-ingame`.

### 4.1 What already landed (all correct, verified)
- **Default sampler** bound with every texture (`metalHW.*`, `metalR_Backend_Runtime.h`). Unbound samplers are UB on Metal and were corrupting all textured draws.
- **`SM_METAL` macro** (`metal_shaders.cpp`) → GL-only Y-flips take the D3D form (Metal RTs are top-left origin, `[0..1]` depth). Applied in the 5 fullscreen stubs + `ssr.h`.
- **SPIRV-Cross `set_enabled_interface_variables(get_active_interface_variables())`** — strips unwritten varyings so PSO validation stops rejecting whole shader pairs.
- Texgen DX11 texel-adjust, sun/rain ortho `[0..1]` depth, sun-shadow TexelAdjust matrices — all switched to the D3D form on Metal.

### 4.2 Remaining work (from the convention audit — 27 confirmed findings)
Prime suspect first:
1. **Dynamic stencil DSS.** `set_Stencil`/`StencilRef` are no-ops on Metal (`metalR_Backend_Runtime.h`, `metalState.cpp`) → the gbuffer stencil mask (ref `0x01`, baked by `blender_deffer_flat`) is never written → the deferred combine's `stencil>=1` test likely gates the whole scene out. **Implement a hashed `MTL::DepthStencilState` cache applied at draw time.** This is the most likely single blocker.
2. **`combine_1.vs`** deferred-composite uv/Y — fix *together* with `combine_2` (`stub_notransform_aa_aa.vs`); their flips currently cancel, so fixing one alone inverts the frame.
3. **accum_direct** NDC/mask quad texcoord tables — adopt the R4 twin's mapping (`r4_rendertarget_accum_direct.cpp`); plus a 3rd `TexelAdjust` site (~L891) and the volumetric texgen (~L1178) still carrying GL `+0.5` Y.
4. Per-pass `CULL_NONE` for fullscreen passes that inherit `CULL_CCW` (bloom, PP, luminance).

Full findings: re-run the audit workflow (it self-documents) or read the commit message of `caa2e4953`.

### 4.3 Metal debug harness (re-add when resuming)
- **Intermediate-RT present:** a temporary `getenv("MTL_DEBUG_BLIT")` switch in `metal_rendertarget_phase_flip.cpp` that blits `position|color|normal|generic0|accum` to the drawable — walks the deferred chain without rebuilds. (Removed for the clean commit; re-add as needed.) **Note:** `rt_Color` is reused mid-frame (accumulator transfer), so an end-of-frame blit of it ≠ the gbuffer albedo — blit mid-frame or check a different RT.
- **Metal API validation:** launch with `MTL_DEBUG_LAYER=1 MTL_DEBUG_LAYER_ERROR_MODE=nslog MTL_DEBUG_LAYER_WARNING_MODE=nslog`. This is what surfaced the unbound-sampler bug ("missing Sampler binding for s_baseSmplr").
- **One-shot `Msg` dumps** + `IWriter`-to-`$app_data_root$` MSL dumps in `metalShaderCompiler.cpp` are the pattern for inspecting generated MSL.

### 4.4 THE shader-sync gotcha (cost a full debug cycle)
The running game loads GLSL from a **copy** at `<CoC gamedata>/shaders/gl/`, **not** from the repo's `res/gamedata/shaders/gl/`. Editing a repo shader has **zero effect** until you `cp` it to the game copy. Symptom: your `#ifdef SM_METAL` fix looks like a no-op and the compiled MSL shows the `#else` branch. **Sync per-file after every shader edit; `grep` the game copy to confirm.** (Don't blanket-symlink — the game set is a CoC-specific superset of the repo base set.)

---

## 5. The active work — AI implementation on GL

The next phase is the **agentic Zone** (`xrMind` / `xrSim`). Read the spec first: [`docs/superpowers/specs/2026-07-05-agentic-zone-design.md`](superpowers/specs/2026-07-05-agentic-zone-design.md).

Shape of it (per the spec and the design decisions in project memory):
- The Zone is **simulated by LLM agents** (persistent `WorldAgents`) that own world evolution. C++ is a thin `xrSim` state store + a near-player executor + materialization back into the engine.
- **Cloud-first, thin offline.** Move the bulk of compute to LLMs (Anthropic/OpenAI) — the local machine's resources are reserved for the renderer/engine, not a load-bearing local model.
- Honest trade-offs the spec commits to: weaker determinism (replay via recorded tool-calls), effectively online, ~\$3–8/wall-hr.
- Invariants that carry over from the engine work: **stable IDs never regenerated**, **two-phase validation / no-THROW** (see §2), materialization contract.

Because GL already runs the full game, the AI work does **not** need the renderer rewrite — build against the GL runtime and the agent bridge (§3) as the verification harness.

---

## 6. Conventions & key directories

- Renderer branches: `#if defined(USE_DX11)` / `#elif defined(USE_OGL)` / `#elif defined(USE_METAL)`. Keep GL/DX11 output unchanged when touching shared code.
- Each renderer module compiles with `RENDER_NAMESPACE=render_gl` / `render_metal`.
- Log conventions: `Msg("* info")`, `Msg("! error")`, `Msg("~ warning")`. Assertions: `VERIFY`, `R_ASSERT`, `THROW` (= `VERIFY` on macOS).
- `src/Layers/xrRenderPC_GL/` + `src/Layers/xrRenderGL/` — the working GL renderer.
- `src/Layers/xrRender/` + `src/Layers/xrRender_R2/` — shared render code / CRender phases.
- `src/xrEngine/AgentBridge.{h,cpp}` — the test bridge. `tools/agentctl.py` — its client.
- `src/xrGame/` — game logic, ALife, Lua bindings (the surface the AI rehaul will reshape).

## 7. Where the detailed context lives
- Roadmap / vision: `docs/superpowers/specs/2026-07-05-engine-roadmap-v2.md`
- AI design: `docs/superpowers/specs/2026-07-05-agentic-zone-design.md`
- macOS setup: `docs/macos-dev-setup.md`
- `CLAUDE.md` — project guide (build, constraints, layout).
