# OpenXVibeRay Engine v2 — Refactor & Build Roadmap

**Date:** 2026-07-04
**Status:** Approved direction (rz), staged execution
**Supersedes scope of:** `2026-03-29-macos-metal-port-design.md` (absorbed as Stage R1)

## Vision

Transform this X-Ray fork into a **native macOS engine**: Metal-first rendering built for Apple GPUs (TBDR, unified memory, raytracing-capable), a rewritten simulation/AI core with **pluggable AI providers** (LLM APIs — Claude first) steering NPCs, squads, factions, and the world simulation, and an engine that a coding agent can **drive and verify autonomously** (agent bridge).

**Principles**

1. **Native over portable.** macOS/Apple Silicon is the primary target. New code may use Metal/POSIX/Apple idioms directly. OpenGL stays compiling as the fallback/reference until Metal reaches parity, then is deprecated behind a flag. Windows/DX11 receives no new investment.
2. **Unified memory is the architecture, not an optimization.** No staging-copy patterns; `MTLStorageModeShared` and GPU/CPU co-authored data structures by default.
3. **Stage, don't leap.** Every stage leaves the game playable and verifiable. Parity first, then rearchitect, then extend.
4. **The engine tests itself.** The agent bridge (workstream T) is a first-class subsystem; every stage's exit criteria are scriptable through it.
5. **Content survives.** The AI rewrite replaces the data model, but importers keep CoC-era content (levels, spawns, smart terrains) playable. Script-level mod compatibility is explicitly **not** preserved.

---

## Workstream R — Rendering

### R1: Metal parity port (in flight)

The existing SP2 plan (`2026-03-29-sp2-metal-renderer.md`, Tasks 1–24). Compatibility-layer Metal backend: shared render code speaks uint64_t handles, shaders arrive via GLSL → SPIR-V (glslang) → MSL (SPIRV-Cross), architecture mirrors the GL backend.

- **Goal:** the full CoC scene renders on Metal with visual parity to GL.
- **Exit criteria:** parity checklist from the SP2 plan Task 24, verified via agent bridge screenshot comparison; stable 60fps @1080p (should beat GL).
- **Status:** foundation merged to dev; Tasks 11–16, 19 in progress; 17–18, 20–24 next.

### R2: Native Metal rearchitecture

Shed the D3D9-shaped abstraction inside the Metal backend (GL keeps the old path):

- **Render graph:** explicit frame graph over `MTLRenderPassDescriptor`s designed for TBDR — pass merging, `MTLStorageModeMemoryless` for intra-frame G-buffer attachments, load/store actions computed from the graph.
- **Unified memory:** single per-frame arena (triple-buffered) for constants/transients; `MTLHeap` placement for render targets; zero staging copies for dynamic geometry.
- **Bindless:** argument buffers for material/texture tables; residency via `useResource`.
- **MSL-native shaders:** translated-GLSL shaders replaced incrementally by hand-written MSL for the hot passes (G-buffer, lighting, combine); the translation pipeline remains for the long tail.
- **Modern presentation:** CAMetalLayer HDR/EDR readiness, ProMotion-aware pacing, MetalFX upscaling hook point.
- **Exit criteria:** measurable frame-time reduction vs R1 (target ≥30% GPU time), zero per-frame allocations, Xcode Metal frame-capture clean of validation warnings.

### R3: Raytracing capability

- `MTLAccelerationStructure` build path from level geometry (static BLAS at level load, dynamic instances per frame via unified memory).
- First features: RT sun shadows and RT AO as hybrid passes (raster G-buffer + RT lighting terms), toggleable against the shadow-map path.
- The R2 render graph gains RT pass nodes; no R1-era code needs to know.
- **Exit criteria:** RT shadows visually correct vs shadow maps on M-series hardware, frame budget respected (dynamic resolution or half-res RT with denoise).

---

## Workstream A — AI & Simulation

### A1: Simulation core v2 (full rewrite, new data model)

New C++20 simulation module (`xrSim`), replacing ALife's object registry/switch machinery:

- Deterministic fixed-tick world simulation decoupled from rendering; entities with needs/goals/relations (factions, economy, territory), online/offline LOD like ALife but with one data model, not two.
- **Content importers:** CoC `all.spawn`, LTX configs, smart-terrain definitions, and level AI-maps import into the new model, so existing levels and populations play. **Breaking:** CoC/Anomaly Lua script mods do not carry over; a new, smaller scripting surface is exposed deliberately.
- Serialization designed for snapshot/restore (enables agent-bridge scenario testing and provider context building).

### A2: Behavior layer

- Utility-scored behavior trees replace the GOAP/motivation stacks for NPC/monster decisions; perception and combat tactics rebuilt on the new entity model.
- Navigation keeps the imported AI-map graph initially (proven data), pathfinding core rewritten (modern incremental A*/HPA on top).

### A3: AI provider interface

`xrAIProvider` — the "connect provider" requirement:

- **Abstraction:** async request/response with structured world-state summaries in, structured decisions out (JSON). Strict time budgets per tier; every call has a local fallback.
- **Decision tiers by latency tolerance:**
  - *Reflex (per-frame):* always local (behavior layer). Providers never in this path.
  - *Tactical (seconds):* squad orders, dialogue lines, NPC reactions — provider-eligible with local fallback.
  - *Strategic (minutes):* faction goals, economy shifts, emergent events, A-Life steering — the LLM sweet spot.
- **Providers:** `LocalProvider` (the A2 behavior layer, always on) and `LLMProvider` (HTTP; Claude API first, provider-agnostic config: endpoint/model/key in LTX). Failure/offline ⇒ silent fallback to local; the game never blocks on a provider.
- **Transport reuses the agent-bridge infrastructure** (same socket/framing code, opposite direction).

### A4: Integration & tuning

Scenario tests through the agent bridge: reproducible sim snapshots, provider on/off A-B runs, behavioral assertions ("squad flanks within 30s", "faction war escalates within N days").

---

## Workstream T — Agent bridge (prerequisite tooling)

Design approved separately (`2026-07-04-agent-bridge-design.md`): Unix-socket control channel (`-agent_bridge`), main-thread command drain, verbs `cmd/lua/key/mouse/state/shot`. Built **first** — it is the verification harness for R1 parity, R2 performance, and all of workstream A.

---

## Sequencing

```
T (agent bridge)  ──────────────►  verifies everything after it
R1 (Metal parity, in flight) ──►  R2 (native rearch) ──► R3 (raytracing)
A1 (sim core v2) ──► A2 (behavior) ──► A3 (providers) ──► A4 (tuning)
```

- T first (small, immediately useful).
- R and A workstreams proceed in parallel; R1 completes before R2 starts; A1 design can start during R1's compile/debug tail.
- GL deprecation gate: after R2 exit criteria hold for two milestones.

## Risks

1. **R1 shader translation** (X-Ray GLSL is non-standard) — mitigated: runtime path first, known problem shaders hand-ported early into the R2 MSL library.
2. **A1 scope** — a full sim rewrite is the largest item; the importer keeps it grounded (playable content from day one) and A1 ships behind a flag alongside classic ALife until parity of "aliveness".
3. **Provider latency/cost** — strict tiering + budgets; strategic tier only by default; all calls observable through the bridge for tuning.
4. **Two big rewrites in flight** — the agent bridge and staged exit criteria are the control rope; no stage starts until the previous stage's criteria are green.
