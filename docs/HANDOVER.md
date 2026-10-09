# OpenXVibeRay — Handover

**Last updated:** 2026-10-09
**Written for:** a fresh coding agent (Codex) or engineer picking this repo up with zero prior context.

---

## 0. TL;DR — current direction

Three things are true right now (2026-10-09):

1. **OpenGL is the working runtime.** The game boots, renders, and is fully playable on the GL backend (`renderer renderer_r3`). The native-renderer rewrite (Metal / Vulkan) stays **paused**: Metal M-R0 (menu) shipped, M-R1 (in-game) is parked with a resume guide (§4). Proposed (pending rz): V1 = GL + agentic Zone, dual-native renderers move to V2.
2. **The AI authority spine (Gate 1, Garbage) runs end-to-end on `codex/ai-authority-spine-gate1`** — deterministically, without an API key: manifest bootstrap → authority shadow/transfer → materialization of real squads and a dog pack on `l02_garbage` → off-frame wakes → validated intents steering the NPCs → ledger/metrics/snapshot → clean reset. Unit tests (23 + 88) green; the Gate 1 smoke is `PASS` (§3.5). Not yet merged to `dev`.
3. **No LLM decision has steered an NPC yet.** Every live wake so far coasted on `missing_api_key`. The next milestone is the first live model run (§5.2), then closing Gate 1 (Tasks 11–13).

So: **build GL, run GL from `/Users/rz/stalkercoc`, build the AI on GL.** Pick up Metal later from §4 if/when someone wants to resume the native-renderer track.

Plan of record for the AI track: [`docs/superpowers/specs/2026-10-09-reanimation-plan.md`](superpowers/specs/2026-10-09-reanimation-plan.md).
Binding AI design: [`docs/superpowers/specs/2026-07-14-full-zone-ai-authority-spine-design.md`](superpowers/specs/2026-07-14-full-zone-ai-authority-spine-design.md) and its Gate 1 plan [`docs/superpowers/plans/2026-07-14-garbage-ai-authority-spine.md`](superpowers/plans/2026-07-14-garbage-ai-authority-spine.md).
Overall vision (carries a 2026-10-09 amendment banner): [`docs/superpowers/specs/2026-07-05-engine-roadmap-v2.md`](superpowers/specs/2026-07-05-engine-roadmap-v2.md).

---

## 1. Repo state & branches

| Branch | State | Notes |
|---|---|---|
| `dev` | main line, GL working; **M-R0 merged** (PR #9); tip `a4d0fa256` (2026-07-18) | base for the AI work; does **not** contain the authority spine yet |
| `codex/ai-authority-spine-gate1` | **active AI branch** (HEAD `3cf0a8543`, 2026-10-09) | `facec9eb9` = rescue commit of the uncommitted 2026-09-06 session (Gate 1 Tasks 1–9: zone manifest, authority registry, intent envelope + atomic commit, decision ledger, world agents, world provider, wake scheduler, materialization, zone runtime + `agent.zone.*` verbs, CLevel live adapter, `xrSimAuthoritySpineTests`). `3cf0a8543` = bridge wiring, materialization, steering, headless-load fixes, Gate 1 smoke. Scheduler heartbeats / event-driven wakes / agent-memory updates are in progress on top. Not merged. |
| `codex/ai-provider-http-transport` | **superseded** — its tip is `dev` (`a4d0fa256`); the spine branch builds on it | xrSim foundation through live Anthropic actor adapter, cancellable off-frame provider queue, and bridge-driven session pack wakes. |
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
- **Run dir: `/Users/rz/stalkercoc`** — the base install (full CoC, ~8 GB) and the only directory with `database/`. Everything lives under it: `appdata/user.ltx`, socket `appdata/agent_bridge.sock`, log `appdata/logs/openxray_radik zagirov.log`, saves `appdata/savedgames/`, screenshots `appdata/screenshots/`, and the loaded GL shader copy under `gamedata/shaders/gl/`.
- The Downloads update package (`/Users/rz/Downloads/S.T.A.L.K.E.R. - Call of Chernobyl`, still the `DEFAULT_GAME_DIR` in `tools/gl_macos_soak.py`) has **no `database/`** and cannot run the game. Always pass `--game-dir /Users/rz/stalkercoc` to the soak wrapper and `cd /Users/rz/stalkercoc` before launching by hand.

### 3.2 Launch
```bash
cd /Users/rz/stalkercoc
# menu only:
/Users/rz/OpenXVibeRay/bin/arm64/Release/xr_3da -agent_bridge -nointro
# Gate 1 Garbage save (l02_garbage) straight in:
/Users/rz/OpenXVibeRay/bin/arm64/Release/xr_3da -agent_bridge -nointro -start 'server(garbage_gate1/single/alife/load)' 'client(localhost)'
# save "1" (l05_bar) straight in:
/Users/rz/OpenXVibeRay/bin/arm64/Release/xr_3da -agent_bridge -nointro -start 'server(1/single/alife/load)' 'client(localhost)'
```
- `-nointro` skips the logo videos; their sequencer only advances while frames present, so a headless run would otherwise sit on them.
- For a headless load send `cmd keypress_on_start 0` **before** the `start …` command so the load screen does not wait for a key press (`tools/ai_garbage_gate1_smoke.txt` does exactly this: `cmd keypress_on_start 0`, then `cmd start server(garbage_gate1/single/alife/load) client(localhost)`).
- Saves: **`garbage_gate1`** loads `l02_garbage` and is the Gate 1 save (`tools/ai_garbage_gate1.xrsim` has `save=garbage_gate1`). The older save "radik zagirov - to garbage" actually loads `l05_bar` — do not use it for Garbage work.

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

**Zone runtime verbs (`agent.zone.*`) — the Gate 1 authority spine.** Dispatched by `CAgentZoneRuntime::HandleBridgeVerb` (`src/xrEngine/AgentZoneRuntime.cpp`) into `xrSim::ZoneRuntime::HandleBridgeVerb` (`src/xrEngine/xrSim/xrSimZoneRuntime.cpp`). Run them with the level loaded (`scene=game`), in this order. Sample responses are from `artifacts/ai_garbage_gate1_smoke/summary.json`:

```text
agent.zone.bootstrap /Users/rz/OpenXVibeRay/tools/ai_garbage_gate1.xrsim
  -> scenario=garbage_gate1 level=l02_garbage revision=1 groups=3 agents=9
agent.zone.start [live|null]     # attaches the CLevel adapter + a provider from the env; "null" forces the deterministic coast provider
  -> running provider=anthropic model=claude-sonnet-5 state=coast reason=missing_api_key timeout_ms=30000
agent.zone.status
  -> running=1 bootstrapped=1 attached=1 tick=8 revision=3 agents=9 groups=3 bound=3 materialized=1 inflight=0 queue=0 ledger=0 last_materialize=complete
agent.zone.agents
  -> id=100 scope=0:1 name=zone_director charter_v=1 authority=0
     ... id=201 scope=6:1 name=stalker_squad ... id=202 scope=6:2 name=bandit_squad ... id=203 scope=7:3 name=garbage_dogs charter_v=1 authority=0
agent.zone.shadow all            -> shadowed=9 rejected=0 revision=2
agent.zone.transfer all          -> transferred=9 rejected=0 revision=3
agent.zone.authority             -> audit_records 36 / seq=1 rev=1 scope=0:1 from=0 to=0 author=0 agent=100 accepted=1 reason=registered ...
agent.zone.bindings              # live objects bound per manifest group (ids are game object ids)
  -> bindings=3
     group=stalker_squad handle=16777216 ids=20224,20229,20234 plan=3 combat=0 bound_rev=3
     group=bandit_squad  handle=16777217 ids=20242,20247,20252 plan=5 combat=0 bound_rev=3
     group=garbage_dogs  handle=16777218 ids=20260,20261,20262,20263 plan=4 combat=0 bound_rev=3
agent.zone.wake <agent_id> <reason>          -> request=1 coalesced=0      # off-frame provider wake through the scheduler
agent.zone.intent <agent_id> <subject> <verb> <target> [ttl_ticks]   # deterministic intent through the same stage/commit/ledger path
  e.g. agent.zone.intent 201 stalker_squad move scrapyard_center 600 -> queued request=3
agent.zone.ledger [after_seq] [limit]
  -> seq=1 req=1 agent=201 scope=6:1 base_rev=3 wake=gate1_smoke p_hash=6a478fe8455eb734 r_hash= prov=anthropic model=claude-sonnet-5 verdict=1:coast codec=missing_api_key validation=coast committed_rev=0 deltas=0
     seq=3 req=3 agent=201 scope=6:1 base_rev=3 wake=bridge_inject p_hash=4b263b9b0ad57346 r_hash=4b263b9b0ad57346 prov=bridge_inject model=deterministic verdict=0:accepted codec=injected validation=accepted committed_rev=4 deltas=1
agent.zone.metrics
  -> frame_calls=1873 simulation_ticks=70 provider_wait_frame_us=0 provider_requests=2 accepted_intents=3 provider_coasts=2 provider_errors=0 codec_rejected=0 validation_rejected=0 stale_rejected=0 ... materialized_groups=3 live_commands_applied=3 scheduled=2 dispatched=2 completed=2 ... effective_rpm=20
agent.zone.snapshot              -> xrsim_zone_snapshot_v1 / revision 6 / factions 2 / locations 8 / groups 3 / resources 2 / end
agent.zone.stop                  -> stopped
agent.zone.reset                 -> reset cancelled=0     # releases objectives and destroys every object the runtime spawned
```

A live-accepted model intent will show up in the ledger as `prov=anthropic … verdict=0:accepted codec=decoded validation=accepted committed_rev=N deltas≥1`; nothing has produced that row yet (§5.2).

### 3.4 Gotchas that will waste your time
- **Do NOT use `-nosound`.** It crashes CoC's `sound_theme.script` (`nil played_id`). To silence audio, set in `appdata/user.ltx` (while the game is stopped): `snd_volume_eff 0.` and `snd_volume_music 0.` — keeps the sound subsystem alive so scripts work.
- **Renderer select** is `renderer` in `appdata/user.ltx`: `renderer_r3` = GL (**current default**), `renderer_metal` = Metal. Change while the game is stopped.
- **Locked / occluded screen = black screenshots.** When the Mac screen is locked or the window is occluded, the GL backend reports the device `Lost`: `shot` returns black JPEGs, and (before the bridge-only precache fallback added to `device.cpp` in `3cf0a8543`) the load precache never finished. Simulation, bridge verbs and the zone runtime all still run; only the picture is missing. Unlock the screen for any screenshot that matters.
- **Never send `key escape tap` from a script.** It opens the main menu and pauses the simulation (ticks stop, wakes stall). If a menu is up, use `cmd main_menu off`.

### 3.5 Soak gates (GL stability + Gate 1 smoke)

Before AI work, keep `renderer_r3` green with the GL macOS soak wrapper (always `--game-dir /Users/rz/stalkercoc`):

```bash
python3 /Users/rz/OpenXVibeRay/tools/gl_macos_soak.py \
  --game-dir /Users/rz/stalkercoc \
  --scenario /Users/rz/OpenXVibeRay/tools/gl_load_play_save_load.txt \
  --repeat 5 \
  --artifacts /Users/rz/OpenXVibeRay/artifacts/gl_macos_soak/repeat_5
```

This launches the game from the run directory (with `-agent_bridge -nointro`), drives the bridge, samples FPS/frame progress and RSS, validates screenshots/logs/exit, and writes `summary.json` plus `report.txt`.

**Gate 1 smoke (the repeatable authority-spine check, ~73 s, no API key needed):**

```bash
cd /Users/rz/OpenXVibeRay
python3 tools/gl_macos_soak.py --game-dir /Users/rz/stalkercoc \
  --scenario tools/ai_garbage_gate1_smoke.txt \
  --artifacts artifacts/ai_garbage_gate1_smoke
# expect: "GL macOS soak: PASS"; every agent.zone.* response is in artifacts/ai_garbage_gate1_smoke/summary.json ("events")
```

It loads `garbage_gate1`, sets `g_god on`, bootstraps `tools/ai_garbage_gate1.xrsim`, moves all 9 agents observe → shadow → AI-owned, lets the runtime materialize the 3 groups, wakes agents 201/203 (coasting without a key), injects one deterministic objective per group, reads status/bindings/metrics/ledger/snapshot, screenshots, then `agent.zone.reset`s and quits. Unit tests that back it: `bin/arm64/Release/xrSimAuthoritySpineTests` (23) and `bin/arm64/Release/xrSimWorldStateTests` (88) — build with `cmake --build build --target xrSimAuthoritySpineTests xrSimWorldStateTests -j10`, run from the repo root.

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
- Future provider-backed in-game AI tests should default to the **Sonnet-tier model** (still `claude-sonnet-5` in code and docs — that ID predates the current Sonnet 5.5 generation and needs refreshing; see the reanimation plan §3) before any Opus-quality pass. Keep deterministic-null/replay tests as the CI floor; use Sonnet to measure realistic in-game behavior, cost, RPM pressure, and crash safety.
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
- Env overrides (`LoadAgentProviderConfigFromEnvironment()` in `src/xrEngine/xrSim/xrSimAgentProvider.cpp`): `XRAY_AGENT_PROVIDER`, `XRAY_AGENT_MODEL`, `XRAY_AGENT_API_KEY`, `ANTHROPIC_API_KEY`, `GEMINI_API_KEY`, `XRAY_AGENT_TIMEOUT_MS`. Gemini (`xrSimGeminiTransport`) is auto-selected when only `GEMINI_API_KEY` is set or the model name starts with `gemini` (default `gemini-3.8-flash`) — a drift from the Sonnet-only spec, see the reanimation plan §3. The key is read at `agent.zone.start`, so export it in the shell that launches `xr_3da`. Never log or commit a key.
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

### 5.2 Authority spine checkpoint (2026-10-09)

Branch `codex/ai-authority-spine-gate1` (`facec9eb9` + `3cf0a8543`). Spec: `docs/superpowers/specs/2026-07-14-full-zone-ai-authority-spine-design.md`; plan: `docs/superpowers/plans/2026-07-14-garbage-ai-authority-spine.md`; plan of record from here: `docs/superpowers/specs/2026-10-09-reanimation-plan.md`.

What exists (`src/xrEngine/xrSim/`):
- `xrSimZoneManifest` — version-controlled scenario manifest (`tools/ai_garbage_gate1.xrsim`: 3 groups, 8 anchors, 2 factions, 4 routes, 2 resources, 9 agents incl. zone director, faction/ecology/economy/region minds, 2 squads, 1 pack).
- `xrSimAuthority` — per-scope observe → shadow → transfer → AI-owned registry with a one-writer audit.
- `xrSimIntentEnvelope` + `xrSimIntentCommit` — strict revisioned intent envelopes, staged/atomic commit, stale-revision rejection.
- `xrSimDecisionLedger` — every provider completion and injected intent lands here (`agent.zone.ledger`).
- `xrSimWorldAgents` — persistent scoped agents with charters and memory.
- `xrSimWorldProvider` + `xrSimAgentProvider` + `xrSimAnthropicTransport` / `xrSimGeminiTransport` — world-intent provider (Anthropic / Gemini / recorded / fault), coasts on a missing key.
- `xrSimWakeScheduler` — bounded off-frame wake scheduler (coalescing, cooldown, circuit breaker, in-flight accounting).
- `xrSimMaterialization` — pure materialization bindings; combat handoff releases the objective and re-applies after a calm window.
- `xrSimZoneRuntime` — orchestration (`BootstrapManifest` / `AttachLive` / `Start` / `Stop` / `DetachLive`) and the `agent.zone.*` verbs (§3.3).
- `src/xrGame/Level_agent_zone.cpp` — the CLevel live adapter: spawn, observe, walk-to steering, hit/death ingress, `GE_DESTROY` on reset.
- Tests: `xrSimAuthoritySpineTests` (23, includes `TestBridgeVerbsDriveLiveRuntimeWithoutKey` and `TestMaterializerHandsOffCombatAndReacquiresWhenCalm`) + `xrSimWorldStateTests` (88).

Verified today: the Gate 1 smoke (§3.5) is `PASS` — 3 groups materialized, 3 injected objectives accepted and applied (`live_commands_applied=3`), 2 provider wakes coasted on `missing_api_key`, `codec_rejected=0`, clean reset.

What is open:
- **First live model-steered NPC.** No ledger row with `prov=anthropic verdict=0:accepted` exists yet. Next: pick the provider policy, refresh the model ID, export a key in the launching shell, run the smoke with `agent.zone.start live`, read the ledger, tighten the world-intent prompt until `codec_rejected`≈0, screenshot with an unlocked screen.
- **Gate 1 plan Tasks 11–13:** Task 11 deterministic replay preflight (`xrSimReplay`, recorded `.intent` files, fault injection); Task 12 `tools/ai_zone_soak.py` 60-minute immortal-player harness + `acceptance.json`; Task 13 live Sonnet gate + acceptance report.
- **In progress on the branch (another agent):** scheduler heartbeats, event-driven wakes, agent-memory updates from observed outcomes — treat as landing, not landed, until the commit is on the branch.
- Save/load of agent memory is deferred to Gate 2 by design.

Current boundary / next AI milestone:
- Committed objectives **do** steer the materialized X-Ray squads and packs through the compatibility executor (`move` / `hold` to manifest anchors), but every objective so far came from `agent.zone.intent` (deterministic). The first model-authored objective is the milestone.
- The executor vocabulary is narrow (move/hold); widening it through smart-terrain / `xr_logic` hooks and adding more event ingress (beyond hit/death) is Phase 3 of the reanimation plan.
- Authority transfer is proven per scope on the Gate 1 manifest only; cross-level continuity, offline Zone evolution and global planner retirement are Gates 2–4.

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
- **AI plan of record (start here):** `docs/superpowers/specs/2026-10-09-reanimation-plan.md`
- AI authority spine (binding design): `docs/superpowers/specs/2026-07-14-full-zone-ai-authority-spine-design.md`; Gate 1 implementation plan: `docs/superpowers/plans/2026-07-14-garbage-ai-authority-spine.md`
- Roadmap / vision: `docs/superpowers/specs/2026-07-05-engine-roadmap-v2.md` (2026-10-09 amendment banner: V1 = GL + agentic Zone, proposed)
- Earlier AI design (xrMind/xrSim framing, still the vocabulary): `docs/superpowers/specs/2026-07-05-agentic-zone-design.md`
- macOS setup: `docs/macos-dev-setup.md`
- `CLAUDE.md` — project guide (build, constraints, layout).
