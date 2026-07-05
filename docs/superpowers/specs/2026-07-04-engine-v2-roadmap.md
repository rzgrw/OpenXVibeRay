# OpenXVibeRay — Version Roadmap (V1 / V2 / V3)

**Date:** 2026-07-04 (rev. 2026-07-05 — restructured around rz's version vision + cutting-edge research)
**Status:** Approved direction (rz)
**Supersedes:** `2026-03-29-macos-metal-port-design.md` (absorbed as the V1 renderer bring-up)

## North Star

Rebuild this X-Ray fork into a **native macOS engine that is the best-performing STALKER engine on Apple Silicon** — modern Metal graphics with raytracing, an AI system built for LLMs and complex simulation, and an engine a coding agent can drive and verify itself.

**The founding principle:** the original X-Ray engine and codebase are **inspiration and foundation, not scripture.** We keep what earns its place (asset formats, game logic, level data, the feel) and shed what doesn't (the D3D9-era render abstraction, the two-headed ALife data model). Faithful preservation is a non-goal; a clean, sharp, fast native engine is the goal.

**Guiding rules**
1. **Native over portable.** New code uses Metal / POSIX / Apple idioms directly. OpenGL stays compiling as the fallback/reference until the native renderer reaches parity, then is retired behind a flag. Windows/DX11 gets no new investment.
2. **Unified memory is the architecture.** `MTLStorageModeShared`, on-chip tile memory, zero staging copies — designed in, not bolted on.
3. **Stage, don't leap.** Every stage leaves the game runnable and verifiable through the agent bridge. Prove correctness, then chase performance, then extend.
4. **The engine tests itself.** The agent bridge is a first-class subsystem; every stage's exit criteria are bridge-scriptable.
5. **Content survives, mods don't.** Importers keep CoC-era levels/spawns/smart-terrains playable. Script-level (Lua) mod compatibility is deliberately dropped in the AI rehaul.

---

# V1 — Native engine: renderer, then AI

Three epics. **Renderer first, AI after.** Goal: Metal renderer stabilized and working with existing game resources **on par or faster** than the old GL path, with raytracing plumbing in place and modern graphics techniques; AI rehauled for LLMs + simulation. Best possible performance on unified-memory M-series.

## Epic T — Agent bridge ✅ DONE

Unix-socket control channel (`-agent_bridge`) — verbs `hello/cmd/lua/key/mouse/state/shot/bye`, `tools/agentctl.py` client. It is the verification harness for everything below (parity screenshots, perf capture, AI scenario replay). Spec: `2026-07-04-agent-bridge-design.md`. Built and proven.

## Epic R — Renderer (native Metal, staged)

**Decision (rz): bring-up first, then native.** The compat-layer Metal backend we already built (`uint64_t` handles mirroring D3D9/GL) is a **crutch for exactly one milestone** — get a correct frame and a parity capture — then it is frozen and never built upon. The native renderer is designed fresh.

**Decision (rz): RT-ready, defer depth.** Build acceleration-structure plumbing into the render graph early; ship V1 with one or two RT effects (shadows first), expand later.

### R0 — First frame (compat-layer crutch) — IN PROGRESS
The backend compiles/links (8.7 MB dylib, `XRAY_METAL=ON` links). Remaining is SP2 Tasks 22–24:
- First boot with `renderer renderer_metal`; first clear color; first geometry; then full CoC scene.
- **Exit:** CoC menu + a loaded level render on Metal; bridge screenshot ≈ GL. This is the *only* milestone the compat layer needs to reach. First known runtime bug: a blender `VERIFY` at device create.

### R1 — Native TBDR deferred core
Freeze the compat layer; build the native path.
- **Single-pass deferred in tile memory** exploiting TBDR: `imageblock<T>`, tile shaders (`dispatchThreadsPerTile`), `MTLStorageModeMemoryless` G-buffer attachments, `[[color(n)]]` framebuffer fetch, raster order groups. All shipping on every M-series.
- **Load-bearing design choice:** the G-buffer emits **signed-format world normals + diffuse albedo + specular albedo + linear roughness** from day one — the exact channels the MetalFX denoiser and RT later require, so they drop in free.
- **Exit:** full scene at or below GL frame time; frame-capture clean of validation warnings.

### R2 — GPU-driven + modern presentation ("faster than GL")
- GPU-driven rendering in strict order: **bindless (argument buffers tier 2) → indirect command buffers (ICBs) → `MTLResidencySet`**.
- **MetalFX** spatial then temporal upscaling (needs motion vectors + depth + jitter — already produced by the deferred core).
- **EDR/HDR** via CAMetalLayer, **ProMotion / adaptive frame pacing** (watch: SDL2 may hide the present path — may need a direct CAMetalLayer present hook).
- **Exit:** measurably faster than GL at equal quality (target ≥30% GPU-time reduction); zero per-frame allocations.

### R3 — Hybrid raytracing (RT-ready, one/two effects)
- `MTLAccelerationStructure`: static BLAS at level load, refit actor BLAS, rebuild TLAS per frame (unified memory, GPU-driven/indirect builds).
- Hardware `intersector<>` (M3+ Family 9, hardware traversal + Dynamic Caching) — **not** inline `intersection_query` on hot paths. **RT shadows first** (`accept_any_intersection(true)`), traced from fragment/tile stages to keep intermediates on-chip.
- `MTLFXTemporalDenoisedScaler` so 1–2 samples/ray suffice.
- **Version policy:** dual Metal 3 / Metal 4 paths; RT effects gate on M3+, everything else runs on all M-series. Mesh shaders + neural materials are R&D, not V1 deps.
- **Exit:** RT shadows correct vs shadow maps, within frame budget on M3/M4; toggleable.

## Epic A — AI rehaul (LLM-backed, after the renderer)

**Deterministic sim core owns all state; the LLM is an advisor, never the authority.** The LLM emits only schema-constrained *intents*; a validator maps them to legal game actions (fits `XRAY_EXCEPTIONS=0` cleanly — a parse/validation failure is a value, not a throw). Providers: Anthropic and OpenAI, **subscription-first** in the first iteration.

### A1 — Simulation core v2 (`xrSim`)
Deterministic fixed-tick world sim replacing ALife's registry/switch machinery: one data model (not online/offline duplicated), entities with needs/goals/relations (factions, economy, territory). **Importers** bring CoC `all.spawn`/LTX/smart-terrains/AI-maps into the new model so existing content plays. Snapshot/restore serialization (feeds both agent-bridge scenario tests and LLM context). **Breaking:** Lua script mods do not carry over.

### A2 — Behavior layer (the always-on local tier)
Utility-scored behavior trees replace GOAP/motivation stacks; perception + combat rebuilt on the new entities. Pathfinding core rewritten (incremental A*/HPA) over the imported AI-map graph. This is the reflex tier — and the fallback when no provider is available.

### A3 — LLM provider seam (`xrAIProvider`)
Three latency tiers:
- **Reflex (per-frame):** always local (A2). Providers never here.
- **Tactical (seconds):** squad orders, dialogue, reactions — Haiku / on-device SLM (llama.cpp-Metal), async, local fallback.
- **Strategic (minutes):** faction goals, economy, emergent events, world steering — Sonnet/Opus batched via a per-faction "director."

Cloud via raw **HTTP/SSE** (no C++ vendor SDK); structured outputs / tool-use for deterministic intents; faction-scoped memory (not per-NPC frontier calls — cost + coherence). Config (provider/model/key/budget) in LTX. Every call has a bounded budget and a silent local fallback; the game never blocks. Transport reuses the agent-bridge socket/framing code.

### A4 — Integration & tuning
Bridge-scripted scenario tests: reproducible sim snapshots, provider-on/off A/B, behavioral assertions ("squad flanks within 30s", "faction war escalates within N days"), record/replay behind a recordable interface.

## V1 milestone sequence (highest-leverage order)
1. **First Metal frame + capture harness** (R0) — compat-layer crutch reaches its one goal.
2. **Native TBDR deferred core at/below GL frame time** (R1) — pivot committed.
3. **GPU-driven + temporal upscale** (R2) — the "faster than GL" promise met.
4. **Hybrid RT shadows + denoise** (R3) — RT-ready proven.
5. **AI seam: one live faction director through record/replay** (A1→A3 vertical slice) — AI strictly after the renderer.

---

# V2 — Polish & sharpen

Full polish pass once V1 is functionally complete: eliminate obvious code and programmatic issues, retire the GL fallback (after the native renderer holds parity for two milestones), remove dead D3D9-era scaffolding and the compat layer, tighten error handling and resource lifetimes, close the honest-TODO lists accumulated across V1, and harden AI cost/latency. The goal is a codebase that is *sharp* — no rough edges, no "temporary" left standing.

---

# V3 — Rust rewrite for extreme performance

Complete rewrite of the (now clean, well-understood) engine into Rust. Approach: incremental FFI-based migration (`cxx`/`bindgen`) starting from leaf/pure-logic subsystems (sim, math, asset pipeline) toward the graphics/FFI-heavy core; Metal via `metal-rs`; ECS for the sim. V1+V2 exist partly to *earn* this — a sharp C++ engine with clear module boundaries and a self-testing harness is the precondition for a safe rewrite, not a leap of faith.

---

## Top risks (with first moves)
1. **The compat layer becoming the architecture** — freeze it at R0's exit; R1 is a fresh path, not an edit of R0. This is the single most important discipline.
2. **Silent regressions, no test suite** — the agent-bridge soak + parity screenshots are the gate; every renderer stage must pass before merge.
3. **Metal 4 / MetalFX API name drift (beta→GA)** — pin to shipping Metal 3 first; layer Metal 4 behind capability checks; verify selector names against final headers.
4. **Hardware gating fragmenting the fleet** — RT/M3+ features are additive toggles; the base renderer runs on all M-series.
5. **Naive per-NPC frontier LLM calls** (cost/coherence blowup) — faction-scoped director + tiering + budgets from the first line of A3.
6. **SDL2 hiding the present path** from ProMotion pacing — plan a direct CAMetalLayer present hook in R2.

## Research provenance
The concrete technique choices above come from a 13-agent research + adversarial-verify pass (2026-07-05) against WWDC 2021–2025 and Apple docs. Corrected claims (struck from the source brief): ARM Mali bandwidth figures aren't Apple's; "MLX 3× llama.cpp" is false; it's "Foundation Models" not "Core AI"; `output_format` is deprecated. Full brief in the session artifact.
