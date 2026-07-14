# OpenXVibeRay — Handover

**Last updated:** 2026-07-14
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
| `codex/ai-provider-http-transport` | **active AI continuation** | xrSim foundation through live Anthropic actor adapter, cancellable off-frame provider queue, and bridge-driven session pack wakes. |
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
- The Zone is **simulated by LLM agents** (persistent `WorldAgents`) that own world evolution. This is a **complete simulation rewrite**, not a sidecar: legacy ALife becomes the compatibility/materialization surface while `xrMind` + `xrSim` take over world authority.
- **Cloud-first, thin offline.** Move the bulk of compute to LLMs (Anthropic/OpenAI) — the local machine's resources are reserved for the renderer/engine, not a load-bearing local model.
- Future provider-backed in-game AI tests should default to the **Sonnet-tier model** (currently documented as `claude-sonnet-5`) before any Opus-quality pass. Keep deterministic-null/replay tests as the CI floor; use Sonnet to measure realistic in-game behavior, cost, RPM pressure, and crash safety.
- Honest trade-offs the spec commits to: weaker determinism (replay via recorded tool-calls), effectively online, ~\$3–8/wall-hr.
- Invariants that carry over from the engine work: **stable IDs never regenerated**, **two-phase validation / no-THROW** (see §2), materialization contract.

Because GL already runs the full game, the AI work does **not** need the renderer rewrite — build against the GL runtime and the agent bridge (§3) as the verification harness.

### 5.1 xrSim foundation checkpoint

The first AI substrate began on `codex/ai-zone-foundation`; the current continuation is `codex/ai-provider-http-transport`. `src/xrEngine/xrSim/` contains a deterministic coarse `WorldState`, stable index+generation handles, clamped `adjust_population`, an append-only tool log, actor intent validation, and the provider handoff. The C++ substrate remains replayable even though live model output is not deterministic.

Debug bridge verbs are available in `-agent_bridge` sessions:

```bash
python3 tools/agentctl.py <sock> ai.reset
python3 tools/agentctl.py <sock> ai.observe
python3 tools/agentctl.py <sock> ai.inject adjust_population debug_region blind_dog 500
python3 tools/agentctl.py <sock> ai.snapshot
python3 tools/agentctl.py <sock> ai.log
python3 tools/agentctl.py <sock> agent.provider live  # Sonnet-tier provider shell; coasts if no API key
```

Smoke script:

```bash
python3 tools/gl_macos_soak.py --scenario tools/ai_zone_smoke.txt --artifacts artifacts/ai_zone_smoke
```

Thin-harness actor checkpoint:
- `src/xrEngine/xrSim/` now includes debug `SquadAgent` and `MutantPackAgent` records that parse `xrsim_actor_intent_v1`, validate actor actions, and emit deterministic actuator command streams.
- Bridge verbs:
  - `agent.actor.list`
  - `agent.actor.observe squad`
  - `agent.actor.wake squad`
  - `agent.actor.observe mutant_pack`
  - `agent.actor.wake mutant_pack`
  - `agent.actor.commands`
- Session pack verbs create and register real mutant objects for the current level session:
  - `agent.pack.spawn <section> <count> [radius_m]`
  - `agent.pack.list`
  - `agent.pack.observe <pack_id>`
  - `agent.pack.wake <pack_id>`
- Smoke: `python3 tools/gl_macos_soak.py --scenario tools/ai_thin_harness_smoke.txt --artifacts artifacts/ai_thin_harness_smoke`

Provider and asynchronous actor checkpoint:
- Default live provider config is `provider=anthropic`, `model=claude-sonnet-5`, `timeout_ms=30000`.
- Env overrides: `XRAY_AGENT_PROVIDER`, `XRAY_AGENT_MODEL`, `XRAY_AGENT_API_KEY` or `ANTHROPIC_API_KEY`, `XRAY_AGENT_TIMEOUT_MS`.
- Missing key is a normal **coast** value (`reason=missing_api_key`), not an error/throw. With a key present, the live provider shell now has an optional curl-backed HTTP transport when `XRAY_AGENT_HTTP=ON` and CMake finds `CURL::libcurl`; otherwise it still coasts with `network_adapter_not_linked`.
- `agent.provider live` reports the effective shell state and `transport=<curl|unavailable>` through the bridge; deterministic null remains the default runtime provider.
- The Anthropic Messages API envelope is shaped locally (`POST /v1/messages`, API version `2023-06-01`, JSON body with `model`, `max_tokens`, `messages`). Coarse Zone responses decode as `xrsim_agent_response_v1`; actor/pack responses decode as `xrsim_actor_intent_v1`.
- Live actor and pack requests now run through one cancellable provider worker. Wake preparation copies all context, the worker owns the synchronous curl call off the frame thread, and polling applies the validated result back on the engine/bridge thread. `ai.reset` and shutdown cancel in-flight curl work.
- Live bridge flow:

```bash
python3 tools/agentctl.py <sock> agent.actor.wake squad live
python3 tools/agentctl.py <sock> agent.actor.poll 1
python3 tools/agentctl.py <sock> agent.pack.wake '1 live'
python3 tools/agentctl.py <sock> agent.actor.poll 2
```

  A wake returns `... queued request=N`; poll returns `pending` or the applied result with provider/model/coast metadata. Deterministic wake payloads remain backward compatible. The older coarse `agent.wake live` path is still a synchronous diagnostic; do not use it from a frame-critical scheduler.

- **Real Anthropic acceptance status (2026-07-14):** API-key propagation, `state=ready`, curl transport, and off-frame execution are proven; frames continued advancing during every live request. The model added a prose/fence preface and echoed `agent_id`, then `scope`, exposing that the actor prompt did not define its response grammar precisely enough. The codec now tolerates the same preface/CRLF and numeric metadata already accepted by the coarse codec, and the prompt now lists every legal output record while explicitly forbidding prose, Markdown, and observation echo. Unit tests and the full build cover these changes, but the strengthened prompt still needs one fresh-key live non-coast acceptance run. Do not claim that final gate green yet.

Current boundary / next AI milestone:
- Actor commands are validated and retained in the xrSim command ledger, and coarse tools such as `adjust_population` mutate `WorldState`, but movement/combat commands are **not yet steering the spawned X-Ray objects**. Wire the command stream into the near-player ALife/monster executor without giving the LLM direct per-frame control.
- Replace manual bridge dispatch with the surprise/cadence scheduler, bounded in-flight accounting, cost/token ledger, and record/replay packets from the agentic-Zone spec.
- Transfer authoritative lifecycle ownership from legacy ALife to xrMind/xrSim in slices; the current session pack registry is a verified materialization seam, not the completed ownership inversion.

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
