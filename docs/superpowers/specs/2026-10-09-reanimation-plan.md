# AI Track Reanimation Plan — Gate 1 to Gate 4

**Date:** 2026-10-09

**Status:** Plan of record for the AI track going forward. The authority-spine design (`2026-07-14-full-zone-ai-authority-spine-design.md`) and its Gate 1 plan (`docs/superpowers/plans/2026-07-14-garbage-ai-authority-spine.md`) remain the binding contracts; this document records where the work actually stands today and the order in which it resumes. Items marked *proposed* are pending rz confirmation.

**Branch:** `codex/ai-authority-spine-gate1` — `facec9eb9` (rescue commit of the uncommitted 2026-09-06 session: Gate 1 Tasks 1–9) and `3cf0a8543` (bridge wiring, materialization, steering fixes). Not merged to `dev` (`a4d0fa256`, 2026-07-18).

## 1. Status (verified 2026-10-09)

The Gate 1 authority spine runs end-to-end, deterministically and without an API key, on a real `l02_garbage` save. The one thing it has not done yet is take a decision from a model.

- **Spine path proven:** `agent.zone.bootstrap` → `start` → `shadow all` → `transfer all` → manifest materialization (3 groups, 10 live objects bound) → off-frame scheduler wakes → `agent.zone.intent` objectives steer the squads and the dog pack through the CLevel executor → `status` / `bindings` / `metrics` / `ledger` / `snapshot` → `reset` destroys everything the runtime spawned.
- **Repeatable smoke:** `python3 tools/gl_macos_soak.py --game-dir /Users/rz/stalkercoc --scenario tools/ai_garbage_gate1_smoke.txt --artifacts artifacts/ai_garbage_gate1_smoke` → `GL macOS soak: PASS` in ~73 s with `codec_rejected=0 validation_rejected=0 materialize_failures=0 live_commands_applied=3`.
- **Unit tests green:** `bin/arm64/Release/xrSimAuthoritySpineTests` (23) and `bin/arm64/Release/xrSimWorldStateTests` (88); build with `cmake --build build --target xrSimAuthoritySpineTests xrSimWorldStateTests -j10`, run from the repo root.
- **Run dir:** `/Users/rz/stalkercoc` (socket `appdata/agent_bridge.sock`, log `appdata/logs/openxray_radik zagirov.log`, saves `appdata/savedgames/`). The Downloads directory still named by `tools/gl_macos_soak.py DEFAULT_GAME_DIR` has no `database/` and cannot run the game — always pass `--game-dir /Users/rz/stalkercoc`.
- **Gate 1 save:** `garbage_gate1` (loads `l02_garbage`); `tools/ai_garbage_gate1.xrsim` has `save=garbage_gate1`. The older save "radik zagirov - to garbage" actually loads `l05_bar`.
- **Headless:** launch flags include `-nointro`; the scenario sends `keypress_on_start 0` so loads do not wait for a key. With the Mac screen locked/occluded the GL device reports Lost — screenshots come back black, and loads only complete thanks to the bridge-only precache fallback in `device.cpp` (`3cf0a8543`). Never send `key escape tap` from a script (opens the menu, pauses simulation).
- **Provider config** (`LoadAgentProviderConfigFromEnvironment()`, `src/xrEngine/xrSim/xrSimAgentProvider.cpp`): env `XRAY_AGENT_PROVIDER`, `XRAY_AGENT_MODEL`, `XRAY_AGENT_API_KEY`, `ANTHROPIC_API_KEY`, `GEMINI_API_KEY`, `XRAY_AGENT_TIMEOUT_MS`. Defaults `provider=anthropic model=claude-sonnet-5 timeout_ms=30000`; Gemini is auto-selected when only `GEMINI_API_KEY` is set or `XRAY_AGENT_MODEL` starts with `gemini` (default model `gemini-3.8-flash`). No key → `state=coast reason=missing_api_key` — a value, never an error. The key is read at `agent.zone.start`, so it must be exported in the environment that launches `xr_3da`. Never log or commit a key.
- **No LLM decision has steered an NPC yet.** Every live wake so far coasted on `missing_api_key`; the only accepted intents were `prov=bridge_inject` (deterministic). The first live run is the next milestone.
- **In progress on the branch (another agent, today):** scheduler heartbeats, event-driven wakes, and agent-memory updates from observed outcomes. Details land in the commit, not here.
- **Open from the Gate 1 plan:** Task 11 deterministic replay preflight (`xrSimReplay`, recorded `.intent` files, fault injection), Task 12 `tools/ai_zone_soak.py` 60-minute immortal-player harness + `acceptance.json`, Task 13 live Sonnet gate + handover.
- `ixray/` (untracked clone of ixray-1.6-stcop at the repo root) is gitignored and reference material only.

## 2. Framing

- **What this is:** an open engine plus an open AI world layer — bring your own game data. Call of Chernobyl 1.4.22 on macOS/GL is the verification target, not a dependency of the design.
- **Division of labor:** *LLM owns decisions, C++ owns embodiment.* World agents decide intent, tactics, memory, ecology and off-screen outcomes through revisioned intent envelopes; `xrSim` owns facts, validation, the ledger and authority; X-Ray owns pathing, animation, perception, combat contact, spawning and save/load. No model call is ever on a rendered frame's critical path; a late or missing provider coasts.
- **Definition of "fully LLM-controlled":** the Gate 1→4 ladder in the authority-spine spec. Gate 1 Garbage vertical slice (one uninterrupted session, immortal player); Gate 2 cross-level continuity, then save/load of agent state; Gate 3 all-level offline Zone; Gate 4 full live authority with legacy planners disabled. Gate 1 proves the spine; only Gate 4 is "done".

## 3. Drift to resolve

| Item | Spec / older docs | Code today | Resolution |
|---|---|---|---|
| Provider policy | Sonnet-only; no Opus/Haiku/OpenAI/local routing (spine spec §3) | `xrSimGeminiTransport` exists; Gemini auto-selected by key or model name | *Proposed:* Anthropic Sonnet is the acceptance provider for every gate; Gemini is a cheap dev/smoke alternative and never a gate. Document in the spine spec once confirmed. |
| Model IDs | "Sonnet-tier" | `claude-sonnet-5` default in `xrSimAgentProvider.h`, README, HANDOVER — predates the current Sonnet 5.5 generation | Refresh in Phase 1 (code change belongs to the AI-track agent; this document only flags it). |
| Roadmap V1 | `2026-07-05-engine-roadmap-v2.md`: V1 = dual-native renderers + agentic Zone + bridge | Renderers paused since 2026-07-06; all effort on GL + AI | *Proposed:* V1 = GL runtime + agentic Zone; dual-native renderers move to V2. Amendment banner added to the roadmap. |
| Run dir | HANDOVER §3.1 and `DEFAULT_GAME_DIR` point at the Downloads update package | Only `/Users/rz/stalkercoc` has `database/` | Docs corrected today; `DEFAULT_GAME_DIR` left to the tools owner. |
| `ixray/` | — | Untracked reference clone, gitignored | Reference only: never built, never merged, nothing links to it. |

## 4. Plan

### Phase 0 — Rescue & re-baseline (DONE 2026-10-09)

- Rescued the uncommitted 2026-09-06 implementation as `facec9eb9`; fixed bridge verb split, null-scheduler deref, materialization, steering and headless loading as `3cf0a8543`.
- Both unit-test binaries green; Gate 1 smoke `PASS` on `/Users/rz/stalkercoc`.
- Docs refreshed: HANDOVER, README, CLAUDE.md, this plan, roadmap amendment banner.

### Phase 1 — First model-steered NPC (next)

Goal: one `agent.zone.ledger` row with `prov=anthropic … verdict=0:accepted codec=decoded validation=accepted committed_rev=N deltas≥1`, whose committed objective visibly moves a group.

1. Decide provider policy (§3) and refresh the model IDs in code, README and HANDOVER.
2. Export the key in the shell that launches `xr_3da` (never in a script or commit) and run the Gate 1 smoke; `agent.zone.start` must report `state=ready`.
3. Read `agent.zone.ledger` for the wake rows and `agent.zone.metrics` for `accepted_intents>0`, `input_tokens`/`output_tokens`/`total_latency_ms` > 0.
4. Tighten the world-intent prompt and codec until `codec_rejected`≈0 and `validation_rejected`≈0 across repeated wakes; record every rejection reason in the ledger notes.
5. Screenshot with an unlocked screen and keep the artifacts directory as the first live proof.

Exit: live ledger + metrics + screenshot artifacts referenced from HANDOVER §5.2; measured $/wake, tokens/wake and latency published.

### Phase 2 — Close Gate 1 (Tasks 11–13)

- Task 11: `xrSimReplay`, recorded `.intent` files under `tools/ai_recordings/garbage_gate1/`, injected-failure preflight (timeout, rate limit, codec reject, stale revision) in `xrSimAuthoritySpineTests`.
- Task 12: `tools/ai_zone_soak.py` — phase control for the 60-minute immortal-player script (observe → trespass → warning shots → damage → withdraw → return), RSS/FPS sampling, `artifacts/ai-zone/<timestamp>/acceptance.json`, `tests/test_ai_zone_soak.py`.
- Task 13: full deterministic matrix + standard bridge soak + live Sonnet non-coast proof + 60-minute acceptance report; handover updated with exact commands.
- Absorb the in-progress heartbeat / event-driven wake / memory-update work once it lands on the branch; then merge the branch to `dev`.

Exit: the seven Gate 1 deliverable conditions in spine spec §15 are green with artifacts.

### Phase 3 — Richer embodiment (→ Gate 2 entry)

- Wider executor vocabulary: beyond move/hold — smart-terrain and `xr_logic` hooks for patrol, guard, trade, loot, vocalize, regroup, retreat; mutant den/stalk/ambush behaviors.
- More event ingress: beyond hit/death — sightings, trespass, item pickup, dialogue triggers, time-of-day and weather, so wakes are driven by what happened rather than by cadence alone.
- Agent memory save/load: versioned serialization of charters, beliefs, memories and plans (the data structures Gate 1 already versions), round-tripped through the game save.

Exit: a group survives a save/load with its plan and memory intact; the executor vocabulary covers every verb the Gate 1 charters emit.

### Phase 4 — Gates 2–4

Per spine spec §13: Gate 2 cross-level continuity (adjacent regions, portals, persistent identities across traversal), Gate 3 all-level offline Zone (Region Minds for every level, coarse state evolving while unloaded), Gate 4 full live authority (legacy planners disabled, compatibility adapters only). Each gate gets its own plan; new unresolved behavior gets a focused spec.

## 5. Decisions needed (proposed, pending rz confirmation)

1. **`ixray/` is reference only.** It is not a build input and nothing in `src/` may depend on it; keep it gitignored.
2. **Provider policy.** Anthropic Sonnet (current generation) is the acceptance provider for every gate; Gemini is kept as a dev/smoke alternative and is never used for a gate run. Refresh `claude-sonnet-5` to the current Sonnet ID.
3. **V1 definition.** V1 = GL runtime + agentic Zone (Gate 1 complete and merged). Dual-native renderers (Metal M-R1+, Vulkan V-R0+) move to V2. The roadmap's A0 "agentic vertical slice on GL" is, in effect, what Gate 1 is.
