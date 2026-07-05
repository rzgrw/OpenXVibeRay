# The Live Zone Simulation (xrSim) — Design Spec

> **⚠️ PARTIALLY SUPERSEDED (2026-07-05, same day).** rz pivoted: **LLM agents ARE the world sim**, not a deterministic C++ sim with an LLM advisor. See `2026-07-05-agentic-zone-design.md` (xrMind + xrSim). The **authority model here is inverted** — the deterministic ecology (Lotka-Volterra/Holling) and faction/economy math are DEMOTED (agents author the world; C++ only executes near-player + stores coarse state). **Still valid and carried forward:** the entity/id invariants (stable id never regenerated, two-phase validation, no THROW on Darwin), the coarse region model, the materialization concept, snapshot/restore, and the testability approach. Read the agentic-zone spec as the current design; read this for the surviving substrate.

**Date:** 2026-07-05
**Status:** Partially superseded by `2026-07-05-agentic-zone-design.md` (authority inverted). Substrate/invariants still current.
**Parent:** `2026-07-04-engine-v2-roadmap.md` (V1 Epic A — "the Zone is simulated by the LLM")
**Provenance:** Synthesized from a 7-agent design workflow (A-Life baseline grounded in this repo's OpenXRay source, wildlife ecology, faction/economy, Zone Director interface, sim/LOD architecture, testability) + a skeptical gap-critique, plus a dedicated hierarchical-subagent design pass. All design fragments in the session scratchpad.

> **Reading order:** Part I is the simulation core and the Zone as a living world. Part II is the LLM control hierarchy (how "all complexity processes in the LLM" stays cheap by scoping it). They share one vocabulary: xrSim owns all state; the LLM emits schema-constrained intents; the sim validates and fans them out.

---

# PART I — xrSim: the simulation core & the living Zone

# xrSim — The Live Zone Simulation: Design Brief

*Status: consolidated design brief, ready to become an implementation spec. Synthesizes six design fragments (A-Life baseline, wildlife ecology, human/faction layer, Zone Director interface, sim/LOD architecture, testability) and resolves the accompanying critique. Opinionated where the critique demanded a decision.*

---

## 0. How this brief resolves the critique (read this first)

The critique's one-line verdict is correct and load-bearing: the six fragments, individually strong, smuggled back in **the exact three sins the baseline fragment diagnosed in original A-Life** — a dual authoritative model, unbudgeted claims on the frame, and ID/reference instability across a fidelity boundary — plus a fatal-on-Darwin stale-reference abort. This brief makes the following binding decisions up front; every later section obeys them.

- **One clock (resolves C1).** Base tick = **100 ms integer `tick_count`**. Every subsystem cadence is an integer multiple of it. Wall-clock is a *trigger* only, never a second time-base. (§6.1)
- **One authoritative representation per animal (resolves C2).** Inside the hot bubble, **agents are truth, the cohort is a recomputed aggregate**; outside, **the cohort is truth, no agents exist**. Never both. Conservation is asserted at every transition. (§2.4, §3.2)
- **One global ONLINE budget (resolves C3).** A single admission controller caps *all* behavior-tree entities Zone-wide (fauna + stalkers + zombies together), with priority eviction. Local budgets are subordinate. (§2.3)
- **Two-phase validation, liveness re-checked at apply (resolves C6, the fatal one).** Director intents are validated at ingest *and again at apply-tick* against current state; a dangling reference degrades to an aggregate or is dropped-with-feedback, **never reaches a THROW**. (§5.3)
- **True local extinction, re-colonization by migration only (resolves C4).** No population floor clamp. A wiped region stays empty until animals walk back. Cumulative Director ecology magnitude is integral-bounded per region per game-day. (§3.5)
- **Explicit ID policy across bands (resolves C5).** Anonymous statistical fauna get ephemeral ids scoped to a materialized lifetime; anything referenced by a quest/Director/save is promoted to a persistent dormant record with a permanent id at reference time. (§2.5)
- **Information latency is a first-class layer (resolves H1).** Events propagate with spatial/temporal delay and per-faction knowledge flags. The Zone is separate minds, not one dashboard. (§4.6)
- **Fixed-point for all persisted authoritative state (resolves M2).** Floats are confined to transient ONLINE physics, re-derived on load, firewalled by a hard assertion from anything that persists or feeds a cross-save decision. (§6.2)
- **New-game-only for V1 (resolves L2).** Legacy CoC save import is out of scope; content import produces a canonical post-import snapshot that is the test fixture. (§8)
- **Region = purpose-built coarse partition (resolves L1).** ~30–60 authored regions over the imported game-graph; smart-terrains nest inside regions. Single upstream decision consumed by all layers. (§2.1)

Everything else in the critique is tuning and is folded into the relevant section.

---

## 1. Overview & design pillars

xrSim is a single deterministic fixed-tick simulation that **owns all Zone state**. It replaces original A-Life's two-headed server/client model with one authoritative store whose entities carry a *fidelity band* — a knob on how much is computed, never on where truth lives. Above it sits a three-tier AI stack (reflex / tactical / strategic); the strategic tier is an LLM **Zone Director** that reasons only over Zone/faction/region/species aggregates and emits schema-constrained *intents* the sim validates and executes. Below it, per-frame behavior stays pure deterministic C++.

**Pillars.**

1. **Live Zone.** The whole Zone is always simulated. Regions the player will never visit this session still have factions maneuvering, prices drifting, and animals breeding — cheaply, as aggregate math. "Alive" is a property of the *statistical* layer being always-on, not of spawning things near the player.
2. **Proper wildlife.** Fauna is a real ecology — carrying capacity, predator-prey functional response, packs with territory, migration, feeding, corpse/nutrient cycling, anomaly/radiation habitat effects — that self-balances without micromanagement and can *genuinely* go locally extinct and re-colonize. Not spawn points.
3. **LLM Director, hard-guardrailed.** The frontier model concentrates all *emergent narrative complexity* ("why the Zone shifts") and touches none of the *mechanical fan-out* ("how thousands of entities behave"). It never computes per-NPC-per-tick. It sets direction; the sim owns the rate and the execution.
4. **Determinism.** One integer clock, named seeded PRNG streams, fixed-point persisted state, strictly ordered reductions. Given the same accepted intents and the same snapshot, a run replays bit-identically — the foundation for saves, LLM context, and tests being the *same* codepath.
5. **Performance on unified-memory M-series.** SoA component store, near-memcpy snapshots, three LOD bands making the far Zone almost free, a single global online-agent budget, and a sim thread decoupled from render via an accumulator. Unified memory means sim buffers are directly GPU-readable without copies.
6. **Testability.** Headless fast-forward, fixed-schema metrics, declarative behavioral assertions, record/replay, provider-on/off A/B, and always-on failure-mode watchdogs — all over the existing agent bridge, because xrSim can tick without a renderer and serialize itself.

---

## 2. The simulation core

> **Loading model:** the LOD bands here (ONLINE / OFFLINE / STATISTICAL / DORMANT) map onto X-Ray's classic per-level model — entities on the **currently loaded level** can be ONLINE (physics/behavior/geometry); entities on **other levels** run OFFLINE/STATISTICAL, exactly as ALife does today. The whole Zone is always at least STATISTICAL regardless of which level is loaded, so cross-region interactions (migration, faction pushes, emissions, economy, events) run continuously across the map. Seamless no-loading-screen streaming (which would let neighbor levels also be ONLINE) is **parked** — see `2026-07-05-seamless-zone-streaming-design.md`; not required for the live-Zone concept.

### 2.1 Region partition (the upstream decision)

A **Region** is a purpose-built coarse partition of the Zone: **~30–60 authored regions** laid over the imported CoC game-graph, with **smart-terrains nested inside regions**. This single definition feeds every layer: ecology gets `area`/adjacency for carrying capacity and migration; the human layer gets ownership granularity and trade-route topology; the Director gets a token-affordable map; the statistical band gets its aggregation unit. Regions form an adjacency graph (the migration/travel/diffusion substrate). Cross-level edges come from the imported cross-tables.

### 2.2 Entity / data model (SoA)

One authoritative **structure-of-arrays** store. Entities are `u32` handles (24-bit index + 8-bit generation, dense free-list). Hot columns (chunked 4K/chunk for cache + parallel jobs):

```
id/handle          u32 (index+generation) — STABLE, never regenerated
band               u8   {DORMANT, STATISTICAL-proxy, OFFLINE, ONLINE}
archetype          u16  (species or stalker type)
faction/community  u8
pos_region         u16  (authoritative coarse position)
pos_graph_vertex   u32  (game-graph / level-graph vertex)
pos_offset         fixed16.vec3 (sub-vertex, ONLINE only meaningful)
health             u16  (fixed-point)
needs              u8×N {hunger, fatigue, fear, safety, breeding_urge, orders}
goal_id / target   u16 / u32
squad_id / pack_id u32
smart_terrain/job  u16 / u16
rng_local          u64  (per-entity stream, seeded at spawn)
flags              u16  {named, story, protected, dormant, dirty}
last_think_tick    u64
```

Cold side-table (ONLINE/named only, keyed by handle): inventory, dialogue/relationship memory, animation state, physics-proxy handle, episodic memory. Anonymous fauna never allocate cold state.

**Aggregate records** (see §3, §4): per-region `PopCohort` (per species), `Region` habitat + control + shared fields, `Faction` ledger + relation matrix, `Squad` aggregate, `Trader/Market`, `CarcassResource`/`FloraNode`/scar list. These are *not* second copies of live entities — they are truth only for the band where no live entity exists (see §2.4).

### 2.3 Tick loop and the global online budget

**Accumulator loop** on a dedicated sim thread:

```
sim_accumulator += frame_dt * time_factor;
while (sim_accumulator >= TICK_DT) {   // TICK_DT = 100ms
    step_sim(); sim_accumulator -= TICK_DT; tick_count++;
}
render lerps transforms by alpha = sim_accumulator / TICK_DT;
```

`time_factor` scales the *in-game clock*, never `TICK_DT`, so determinism (integer `tick_count`) survives time dilation, pause, and fast-forward. Spiral-of-death guard caps catch-up at 5 ticks/frame (drops wall-clock, never sim integrity).

**Global ONLINE admission controller (the shared budget, resolves C3).** One Zone-wide hard cap — **target 150–200 behavior-tree entities total**, fauna + stalkers + zombies competing in the *same* pool. Admission priority: player-visible > audible/near > named/story/quest-relevant > rest. When the cap is hit, lowest-priority ONLINE entities **demote to OFFLINE math** (not despawn). This makes a faction war + a dog overpopulation + an emission all landing in one hot region a *graceful degrade*, not a frame cliff. Per-region caps and the fauna `AGENT_BUDGET` are subordinate advisory limits under this global one.

### 2.4 The three LOD bands

| Band | What is computed | Cadence | Position truth |
|---|---|---|---|
| **ONLINE** | Full per-entity: physics, animation, reflex behavior trees | every tick, 3 reflex sub-steps (~30 Hz) | fine level-graph + offset |
| **OFFLINE** | Per-entity *behavioral*: needs, goals, movement along graph, statistically-resolved combat. No physics/animation | round-robin, `tick_count % 5` bucket by `id % 5` → each entity every 500 ms (2 Hz) | game-graph vertex |
| **STATISTICAL** | No per-entity records. Aggregate population counts + flows per region | every 100 ticks (10 s, 0.1 Hz) | region only |

Band membership is recomputed on a coarse 1 Hz pass over region occupancy + player position — not every tick.

**The single-authority rule (resolves C2, C5).** An animal or stalker is represented **exactly once**:
- In a **STATISTICAL** region it exists only as a count inside a `PopCohort` / `Region.population[faction]`. There is no entity, no id, no agent.
- In an **OFFLINE/ONLINE** region it exists as a live SoA entity; the cohort for that (region, species) is a **running aggregate recomputed from the live entities**, held for the Director's reads but *not authoritative* while the region is hot.

Agents are never Poisson-drawn from a count that is *simultaneously* authoritative. The count is authoritative *or* the agents are — determined by band, never both.

### 2.5 Promotion / demotion (with hysteresis, conservation, and ID policy)

**ONLINE ↔ OFFLINE** is a cheap **attach/detach of a physics+animation+render proxy**. Behavioral state is continuous across it. It is **never a spawn/despawn and never a new id** (this is the baseline's headline fix). Hysteresis band: promote at ≤150 m and same/adjacent level, demote at >200 m or level unload — thresholds deliberately different to stop boundary thrash (mirrors original `switch_distance*(1∓factor)`, now as attach thresholds).

**STATISTICAL → OFFLINE (materialize).** When a region enters the OFFLINE ring (player within K hops, or Director-flagged hot), draw concrete entities from `Region.population[faction]` / `PopCohort.count`:
- **Serialized, region-id-ordered, single-threaded pass** (resolves M1). The global budget is drawn *in that fixed order*, so budget exhaustion is deterministic, not "whatever fit first." Materialization is **excluded from the parallel-jobs phase**.
- Counts drawn via the **materialize** PRNG stream seeded from `(region_id, sim_time)`; agents placed on graph vertices weighted by smart-terrain capacity; packs assembled per pack-size distribution; the drawn count is **decremented from the cohort** (conservation).
- **Trickle, not instant** (resolves M3/"pop-in armies"): materialization spreads over N ticks — a bounded number of entities appear per tick until the target is met — so a hot promotion never spawns an army in one frame.
- **ID policy (resolves C5):** anonymous fauna get **ephemeral ids scoped to this materialized lifetime**; they are honestly "a different rat" next time — fine for anonymous wildlife. Anything **referenced** (quest `target_ref`, Director party, named/story, squad leader) is **not anonymous**: it lives as a persistent **dormant record with a permanent id** and materializes *by re-attaching that record*, id unchanged.

**OFFLINE → STATISTICAL (dissolve).** On leaving the ring, fold live entities back into `Region.population` / `PopCohort` (`count += survivors`, `hunger_avg = mean`, record births/deaths that occurred). But:
- **Named / story / quest-referenced / squad-leader / protected entities never dissolve** — they demote to a persisted **DORMANT** slot (kept in the store, skipped by schedulers), permanent id retained.
- **Conservation invariant asserted every dissolve** (as a logged value on XRAY_EXCEPTIONS=0, not a throw): `sum(live + dormant + statistical)` per faction/species is conserved modulo explicitly-logged births/deaths. This is the invariant original A-Life lacked; extended here to fauna, which no fragment had done.

---

## 3. Wildlife ecology

Two layers sharing the single data model, coupled to the existing per-species reflex state machines in `ai/monsters/` (eat/rest/panic/attack/home_point/squad_move) — we **drive existing states, we do not write new locomotion**.

### 3.1 Species roles (fix each species' equations + behaviors)

- **Grazers / prey** (boar, flesh, tushkano, rats) — primary biomass; graze flora nodes; large overlapping herd ranges.
- **Pack predators** (blind dogs, pseudodogs) — territorial hunting packs; intra-guild interference.
- **Ambush predators** (bloodsuckers, snorks, juvenile cats) — solitary/small, high per-kill impact.
- **Apex** (chimera, pseudogiant) — solitary, huge home range, top of chain, low density.
- **Psi / anomalous** (controller, burer, poltergeist) — low density; feed on psi/emotion + fresh corpses; outside normal predation.
- **Scavengers** (rats, tushkano, crows) — opportunistic; path to carcasses.
- **Undead** (zombies/zombified) — **not in the food web**: emission-spawned, decay over time, hunt the living, don't breed or eat. (Kept as a separate overlay; resolving the fragment's open question toward "overlay," with the one concession that decayed undead may become carcass for psi-feeders.)

### 3.2 Population dynamics (statistical layer — the always-on liveness)

Each region holds `PopCohort{species_id, count (fixed-point, fractional allowed), count_prev, biomass, hunger_avg, breeding_accum, births_since, deaths_since, rad_tolerance}`. This is authoritative **only in STATISTICAL regions** (§2.4).

Discretized, density-limited **Lotka-Volterra with Holling Type II** functional response, per ECO_TICK:
```
prey:     dN = r·N·(1 − ΣherbivoreBiomass / K_flora) − Σ_p predation_p
predator: dP = eff·predation − death·P − P·max(0, (P/K_pred_territory − 1))
predation = a·N / (1 + a·h·N)          // Holling II: saturating intake
```
Semi-implicit Euler, fixed dt = ECO_TICK, per-tick delta clamped to ±CAP_FRAC·count for numerical stability. `K_flora`, `K_pred_territory` derive from region area, flora density, shelter count — this carrying capacity is what auto-balances the system so nothing explodes without micromanagement.

**Balance guarantee — corrected from the fragment (resolves C4).** The fragment's `refuge_min` floor is **removed**. Instead:
- **True local extinction is allowed.** Wipe every dog in a region and it stays empty.
- **Re-colonization is by migration only** — neighbor immigration walks animals back over game-time. Believable, non-permanent, and not a spawn point.
- The **balance guardian** still runs each ECO_TICK for *high* watermarks (density-death, emigration pressure) — it corrects overpopulation before the Director is consulted, so the sim is self-stable on the top end without a fake floor on the bottom.

### 3.3 Per-band behavior

- **STATISTICAL:** the LV/flow math above. ECO_TICK = **300 ticks (30 sim-min)** at 100 ms base; on time-skip (sleep/fast-travel) a **catch-up integrator batches many ECO_TICKs** deterministically.
- **OFFLINE:** per-entity needs + goal drift at 2 Hz; movement along graph; feeding/breeding as discrete events at dens/flora.
- **ONLINE:** needs update every ~2 s (AGENT_TICK), utility state-selection every 0.5 s, motion/aim per-frame via existing reflex trees. A utility selector scores existing reflex states by `need × context`.

### 3.4 Packs, territory, feeding, migration

- **Packs** = leader + members + soft-circle home range + den. Leader pathfinds; members steer via boids (cheap believable flocking). Home range is a spring pull so packs hold territory; conspecific packs repel (the intra-guild term).
- **Feeding/breeding cycle:** grazers deplete logistically-regenerating flora nodes; predators/scavengers consume carcasses or make kills. Breeding accumulates only when `density < K` and hunger is low — starvation and overcrowding cap it naturally.
- **Corpse/scavenging loop:** every death emits `CarcassResource{pos, biomass, decay_t, radiation}`; scavengers and psi-feeders draw from it; it decays over ~a game-day, returning biomass to flora (closes the nutrient cycle) or feeding an anomaly if it fell in one.
- **Migration:** `MigrationPressure(R→R') = f(overcrowding, hunger, predator_load, safety, corridor_cost)` on the region adjacency graph; each ECO_TICK a fraction of a cohort diffuses along the steepest gradient edge. This is also the **re-colonization channel** for extinct regions (§3.2).

### 3.5 Anomaly, radiation, day/night, emission

- **Anomalies:** lethal terrain with high pathfinding cost; fauna route around, but high-fear panic can drive them in → death → carcass → the anomaly feeds.
- **Radiation:** per-species `rad_tolerance`; intolerant fauna avoid, tolerant mutants use rad zones as predator-free **refuges/breeding grounds** — distinct micro-habitats. Chronic rad slowly raises aggression/mutation params the Director can read.
- **Day/night:** a shared `daytime[0..1]` scalar scales needs-growth, detection, and speed (nocturnal predators, diurnal grazers), clustering hunts at dusk/dawn. **This same clock is read by the human layer and anomalies too** (resolves H4) — not fauna-only.
- **Emission** is the master disturbance, but **tick-scheduled and staggered** (resolves M3): a deterministic scheduled event, with **per-region aftermath staggered** rather than a Zone-wide synchronized shock, and materialization trickled. Warning phase → fauna fear spike → forced migration to shelters; blast → unsheltered cohort fraction dies → carcasses + a zombie cohort seeded from stalker deaths; aftermath → anomaly fields rerolled (fresh artifacts, see §4.5). Bounded to avoid the boom/bust resonance the critique flagged.

### 3.6 Director seam for ecology

Guardian runs each ECO_TICK and self-corrects **before** the Director is ever consulted. The Director receives only per-region `EcologyCohortSummary{counts, 3-tick trend, hunger, notable events}` and may emit bounds-clamped `EcologyIntent`: `SET_MIGRATION_PRESSURE`, `MODIFY_BREEDING(factor∈[0.5,2])`, `SEED_POPULATION(count≤cap)`, `TRIGGER_LOCAL_MIGRATION`, `NUDGE_APEX_TERRITORY`. **Cumulative magnitude per region per game-day is integral-bounded** (resolves C4) — not just per-intent clamped — and guardian corrections are fed back as `director_feedback` so the model sees "your seeds keep getting density-killed" and stops. The Director never addresses an individual animal, sets a raw count, or runs per-frame.

---

## 4. The human layer

Three nested scopes mapping onto the three AI tiers so cost concentrates in the Director.

### 4.1 Factions (strategic scope)

8–12 `Faction{id, ledger{money,ammo,meds,food,artifacts,manpower}, goal_stack[Directive], relation_matrix, home_regions[], frustration}`. The Director does **not** emit squad orders; it emits **FactionDirective** intents (verb ∈ EXPAND/DEFEND/RAID/RETREAT/TRADE_PUSH/RECRUIT/SEEK_ARTIFACT/MAKE_PEACE/DECLARE_WAR, target, intensity, ttl, rationale) that a deterministic C++ **planner** decays into concrete squad orders over many fast ticks.

### 4.2 Squads (tactical scope)

`Squad` runs a deterministic FSM: FORMING → TRAVELING (HPA* over the region graph, cost weighted by `heat`+`mutant_pressure`) → {CAMPING, TRADING, FIGHTING, FLEEING(morale<0.3)} → DISBANDED/DEAD. Members are Stalker records; squad stats are aggregates. Entering the player's loaded region promotes members to ONLINE behavior-tree NPCs (under the §2.3 global budget); leaving demotes them to OFFLINE aggregate math. **Off-screen combat** resolved by a deterministic power/luck Lanchester roll seeded per-squad for replay. **Night behavior** (resolves H4): the FSM gets a night camp/hole-up bias; travel slows; traders have availability windows.

### 4.3 Stalkers (reflex scope)

`Stalker{id, community, rank, experience, personal_goodwill, loadout, needs, squad_id, alive}` — driven by A2 behavior trees when ONLINE, cheap aggregate math otherwise. **Named/story/quest-relevant stalkers carry the `protected` flag** (resolves H3): they are **excluded from statistical Lanchester death** — they die only in explicit ONLINE/OFFLINE resolution — and never dissolve (they demote to dormant, §2.5).

### 4.4 Territory control

`Region.control_strength[faction]`: friendly squad presence adds, enemy subtracts, battles shift it. Crossing a threshold with no enemy present for N ticks flips ownership, updates trader/base allegiance, and emits `WORLD_EVENT(region_captured)` — but this event is **not instantly global** (see §4.6). Contested regions raise the shared `heat` field.

### 4.5 Economy

Per (region, item_class): `price = base · scarcity(stock) · demand(recent_consumption) · danger(heat) · director_multiplier`, the multiplier clamped to **[0.3, 3.0]**. Traders restock from faction ledgers along controlled trade routes; a captured region cuts a route → downstream scarcity → price rise → attracts escort squads and player arbitrage. **Artifacts enter supply only via anomaly fields after emissions** — natural boom/bust — with **price-rebound damping** so the post-emission gold-rush can't resonate into oscillation (resolves M3).

### 4.6 Information latency — the missing believability layer (resolves H1)

Events are **not** instantly globally readable. Each `WorldEvent` has a spatial/temporal radius and a **per-faction `known` flag** that propagates with delay along squad line-of-contact and shared-region presence. A firefight is *heard* by nearby squads (they investigate); a region flip is learned by the capturing faction immediately, by neighbors over hours, by distant factions via rumor/traders. The **Director's digest surfaces only what a faction plausibly knows**, and faction planners read a **knowledge-filtered view**, not raw `control_strength`. This single cheap model is what makes the Zone feel *inhabited by separate minds* rather than driven by one omniscient dashboard — the largest believability item absent from all six fragments.

### 4.7 Emergent quests

A Quest tick runs predicate→offer rules over knowledge-filtered sim state: friendly squad supply<0.25 in hostile region → escort/resupply; base under threat → defend; artifact surfaced near player → retrieval; high-value squad wiped near player → loot/avenge. Each offer is a data record with expiry, a `reward_ref` drawn from a faction ledger, a **completion predicate**, and a **giver-liveness precondition** (resolves H3): if the giver dies/dormants before acceptance, the offer expires cleanly rather than dangling. The Director may attach a one-line narrative hook but **never gates a quest's existence** — quests arise from world state even with the LLM offline.

### 4.8 Reputation & the shared relation matrix (resolves M5)

`PlayerReputation[faction] ∈ [-1000,1000]` (legacy scale retained for content) + per-stalker overrides. The critique correctly flags **three writers** to the faction relation matrix (player-action spillover, Director `faction_relation` intent, C++ decay). Resolution: the matrix has **one deterministic update function per tick** that folds all inputs in a **fixed order — decay → player spillover → clamped Director nudge**. The Director sets *direction*; C++ owns the *rate*. Player spillover **routes through this same function**, never writing the matrix directly. This kills the shoot-on-sight/friendly thrashing.

### 4.9 Cross-layer coupling

Shared per-region fields form the contract between layers (written by whoever owns them, read by all): `heat`, `mutant_pressure`, `food/carcass`, `anomaly_field state`, `danger`. Emissions force squads to shelter (or take an off-screen casualty roll), inject artifacts, and emit a `WorldEvent` the Director later reacts to. Wildlife migrations raise `mutant_pressure` → the economy reads a risk premium, territory reads a control penalty. Layers couple through **data**, not direct calls.

### 4.10 Scars — the memory of violence (resolves H2)

Dissolve-to-statistical discards per-entity detail, so a fallen outpost would otherwise be just a flipped flag. A small **persistent per-region scar list** (destroyed structures, notable corpses with loot, battle sites) **survives band demotion** and re-materializes as static props — capped per region, decaying slowly, decoupled from the fauna carcass cycle. This is the strongest "this happened while I was away" signal.

---

## 5. The Zone Director

The Director never sees the sim; it sees a **WorldDigest** and returns an **IntentBatch**. xrSim is the sole mutator.

### 5.1 World-state summary (WorldDigest)

Built **fresh at fire time** from a copy-on-write snapshot (resolves M4 — the 1/min projection is only a *max-staleness fallback*, always overridden by an event-triggered fresh build). Hand-serialized, hierarchical, **token-budgeted** (target 4–6k in, hard cap 8k), all numerics **bucketed** for stable reasoning:
- **header:** sim_day, tick_seq, last_plan_id, plan_expiry, provider budget remaining.
- **regions[]:** id, biome, control_faction, contested, anomaly/emission risk 0–3, danger 0–3, pop_summary (bucketed), resource level. **Proximity LOD:** near regions full detail, far regions collapse to one line; drop lowest-salience first (`salience = recency · proximity · magnitude`).
- **factions[]:** id, strength band, morale 0–3, territory, **knowledge-filtered** stance matrix (−2..+2), war flags, goal slots (from memory).
- **ecology[]:** species, region, pop_band, prey/predator ratio bucket, migration_urge.
- **economy:** 4–6 goods, scarcity index + trend arrow.
- **player:** home region, rep per faction (−2..+2), ≤5 recent notable actions.
- **recent_events[]:** ≤12 salient one-line typed tokens **that the reasoning scope plausibly knows** (§4.6).
- **director_feedback[]:** prior-tick rejection/guardian reason codes.
- **zone_summary:** the reflection preamble (§5.5).

### 5.2 Intent schema (structured output)

`{plan_horizon_ticks, rationale(≤280 chars, logs only), intents[≤16]}`. Bounded tagged-union verbs (all refs from the digest's id space): `faction_posture`, `faction_relation{delta −1|0|+1}`, `spawn_pressure{delta −2..+2}`, `migration`, `anomaly_event{emission|psi_storm|anomaly_bloom, severity, eta_ticks}`, `economy_shift`, `party_seed`, `quest_seed`, `narrative_beat`, `reputation_nudge`, plus the ecology verbs (§3.6). Unknown verb/field/ref = **rejected as a value**, never thrown.

### 5.3 Validate / apply pipeline (two-phase — the fatal-abort fix, resolves C6)

Deterministic C++, no LLM.
1. **Ingest validation** (against the snapshot the digest was built from): schema/type, reference existence, legality/clamp (intensity/delta ranges; can't retreat from a non-bordered region; migration between non-adjacent regions rerouted or rejected; `spawn_pressure` clamped by region carrying capacity so ecology can't be broken), rate/quota (≤1 anomaly_event per region per M ticks; a faction can't advance on >2 fronts), conflict resolution (contradictory intents: last-writer-wins within batch, logged).
2. Surviving intents compile to an **ExecutionPlan** = ordered `PlanOp`s with per-op schedule offsets spread across `plan_horizon_ticks`.
3. **Apply-tick re-validation (the critical second pass).** Because the sim advanced many ticks during the async call and entities may have materialized/dissolved/died, each PlanOp is **re-checked against *current* state at drain time**. A dangling ref **degrades gracefully** — retarget to the region aggregate, or drop-with-feedback — and **never reaches a THROW**. On XRAY_EXCEPTIONS=0 Darwin an unchecked dangling ref hitting a THROW is a fatal process abort, so this pass is stability-critical.

Auto-repair is limited to bounded, deterministic operations (clamp, reroute-to-adjacent, retarget-to-aggregate); anything more complex is reject-and-feedback, to keep the determinism story clean.

### 5.4 Deterministic multi-tick execution

xrSim drains the PlanOp queue on the fixed tick. Each PlanOp mutates only sim-owned aggregate/spawn/faction state; the **mechanical fan-out is A2's job**: `spawn_pressure` becomes SpawnQuota deltas smart-terrains realize at their own cadence; `faction_posture` sets a region objective squads pull as behavior-tree goals; `migration` writes per-species herd targets wildlife steering follows. Plans carry expiry; on expiry with no new plan, ops fall back to a **steady-state continuation** (hold postures, natural ecology dynamics) so the Zone never freezes. Plan swap is atomic at a tick boundary (double-buffered pointer).

### 5.5 Memory & reflection

Zone/faction-scoped (never per-NPC), persisted in the snapshot: a rolling salient-event ledger (~200), per-faction memory blobs (goals, grudges, wins/losses), and a running Zone narrative summary. Every R strategic ticks (or on ledger overflow) a **reflection call** map-reduces ledger+old-summary into a compressed new summary and prunes raw events — keeps memory token cost flat over a 100-hour campaign. Memory is injected into the digest as faction goal slots + a short Zone-history preamble.

### 5.6 Cadence, non-blocking, cost, hard limits

- **Cadence:** wall-clock base **90 s** (LTX `zone_director_period_s`), early-fire on `event_salience_accum ≥ threshold` or player crossing into a region whose plan is stale; rate-limited (min 30 s between calls, max 1 in flight). Wall-clock triggering, never per-sim-tick, so pause/time-dilation can't spam calls. **The 90 s wall-clock is a trigger; it is not a second clock — all application still happens on integer tick boundaries** (resolves C1).
- **Non-blocking:** `xrAIProvider` runs HTTP/SSE on a worker; the sim never blocks. Deadline = 0.6 × period. A missed/late/failed tick → **world coasts on the last plan**.
- **Cost / fallback ladder:** token budget bucket (`zone_director_daily_token_cap`); on exhaustion or failure: Opus/Sonnet → Haiku → on-device SLM (llama.cpp-Metal, reduced schema) → pure A2 steady-state (Director suspended after K failures, `! ZoneDirector suspended` logged, game fully playable). **Replay pins model+tier+seed** so an Opus-plan vs Haiku-plan divergence is reproducible.
- **Hard limits:** Director MUST NOT touch per-frame anything, individual entity state, the tick loop, RNG, or snapshot bytes; MUST NOT name ids outside the digest space; MUST NOT be on the critical path.

### 5.7 Concrete round-trip example

**Digest excerpt.** `header{day 14, tick 60120, plan p-88 expiring in 3, budget 42k tok}`; `regions[{id:garbage, control:bandits, contested:true, danger:2, pop:{stalkers:8,dogs:12,flesh:5}}]`; `factions{loners strength:med morale:2 territory:[cordon,rostok]; bandits strength:high morale:3 territory:[garbage] stance{loners:-2}}`; `ecology{blind_dogs@garbage pop_band:overpop, prey_predator_ratio:low, migration_urge:2}`; `player{home:rostok, rep{loners:+1,bandits:-2}, recent:['cleared bandit camp in cordon']}`; `recent_events['bandit raid on loner outpost (cordon)','dog pack attacked traders (garbage)']`.

**LLM IntentBatch.** `{plan_horizon_ticks:120, rationale:'Bandits press advantage toward Cordon while overpopulated dogs migrate out of Garbage; seed a defensive loner quest.', intents:[ {faction_posture,bandits,cordon,advance,2}, {faction_posture,loners,cordon,fortify,2}, {migration,blind_dogs,from:garbage,to:agroprom,urgency:2}, {spawn_pressure,garbage,blind_dogs,-1}, {quest_seed,giver:loners,region:cordon,archetype:defend,target_ref:cordon_outpost,reward_band:med}, {narrative_beat,scope:cordon,text_key:beat.bandit_push,trigger:player_enters_cordon} ]}`.

**Validate.** Ingest: all refs exist; bandits border cordon → advance legal; garbage→agroprom adjacent → migration legal; `spawn_pressure −1` within carrying cap; loners hold cordon → quest giver legal. Accept all 6 → **ExecutionPlan p-89, seed 0x5f3a, expiry tick 60240**, ops: 2 FactionObjective sets (immediate), MigrationHerdTarget ramped over 60 ticks, SpawnQuota −1 to garbage dog terrain, QuestSeed registered (with giver-liveness precondition), NarrativeBeat armed.

**Apply-tick re-check (§5.3).** At drain, `cordon_outpost` still live and loner-held → quest stands. Had the outpost fallen in the interim, the quest retargets to `cordon` aggregate or expires with feedback — no abort.

**Execution.** Over ~120 ticks: bandit squads pull the advance objective and push into Cordon via A2; loner squads fortify; dog herds steer toward Agroprom, thinning Garbage; the player entering Cordon triggers the defend quest + narrative beat — all deterministic and replayable from p-89 + snapshot.

---

## 6. Determinism, persistence, threading

### 6.1 One clock (resolves C1)

**Base tick = 100 ms, authoritative integer `tick_count`.** Every cadence is an integer multiple, frozen in a shared constants header:

| Subsystem | Cadence | In ticks |
|---|---|---|
| ONLINE reflex | ~30 Hz | 1 tick, 3 sub-steps |
| OFFLINE behavioral | 2 Hz round-robin (5 buckets) | every tick, `id%5` bucket |
| STATISTICAL / ECO_TICK | 30 sim-min | every 300 ticks (with catch-up batch on time-skip) |
| Band-membership recompute | 1 Hz | every 10 ticks |
| Strategic (Director) | ~90 s wall-clock **trigger** | applied on tick boundary |

Wall-clock exists only as a *trigger* for the async Director; it is never a second time-base.

### 6.2 Determinism (resolves M2)

- **PRNG:** one master seed → `hash(seed, fnv(stream_name))` seeds each named PCG32/SplitMix64 stream (spawn, combat, pathing, economy, wildlife, migration, director_fanout, materialize) + per-entity `rng_local`. Never draw cross-stream; adding a subsystem = a new stream, **zero perturbation** of existing replays.
- **Fixed-point for all persisted authoritative state** — counts, health, positions-on-graph, economy, goodwill, `PopCohort.count`. Floats are confined to transient ONLINE physics that is **re-derived on load** and **firewalled by a hard assertion**: no GPU/physics float result ever feeds persisted state or a cross-save decision (extends the firewall to fauna counts, which the fragment left open).
- **Ordered reductions:** iterate entities/regions in stable id order, never hash-map or thread order. Cross-entity effects go through per-worker command buffers merged single-threaded in id order (see §6.3).

### 6.3 Threading

- **Sim thread** owns `step_sim`. Band updates fan out to a **job pool over SoA chunks**; a job may only write *its own* entity's columns (or double-buffered squad aggregates). Cross-entity effects (damage, perception) are collected into per-worker command buffers and applied in a **single-threaded, id-ordered merge** — identical result regardless of thread count. **Materialization is excluded from the parallel phase** (§2.5, M1).
- **I/O thread** runs `xrAIProvider`; intents land in a lock-free queue drained at a tick boundary.
- **Bridge thread** marshals verbs onto the sim thread at tick boundaries (resolves L4); a soak drives the sim thread directly with periodic drain checkpoints so a multi-day run doesn't make the bridge unresponsive.

### 6.4 Persistence

Snapshot = `{version, content_hash, tick_count, all PRNG states, live SoA column blobs (RLE-skip dormant), dormant records, region statistical arrays, scar lists, Director memory + intent-log tail}`. Near-memcpy on UMA; restore rebuilds transient indices (spatial hash, squad rosters) deterministically from data. **The same codepath serves save-game, LLM-context (a lossy per-region projection), and bridge replay (bit-exact)** — because there is one model, there is no online/offline write-back gap to lose state through. Snapshot is copy-on-write off the sim thread to avoid a stall.

---

## 7. Testability

xrSim is headless-capable and self-serializing, exposed through a small set of agent-bridge verbs.

### 7.1 Three run modes

- **LIVE** — Director calls the provider; intents recorded to the plan log with exact bytes + apply tick.
- **REPLAY** — Director reads recorded plans; no LLM call. Reproducible bug hunts, CI.
- **DETERMINISTIC-NULL / provider-off** — Director disabled; sim coasts on scripted/empty intents. The A/B baseline; token-free.

### 7.2 Metrics

A sampler emits a fixed-schema `MetricRecord` every sim tick to a ring buffer + optional NDJSON sink: per-species pop/births/deaths/migrations; per-faction territory/squads/casualties/kills/money; economy (money supply, artifact price index, trades, spike flag); event counts by type; Director health (proposed/accepted/rejected, plan latency, fallbacks, tokens). Ring buffer holds ~28 in-game hours at 1 Hz; multi-day soaks stream to NDJSON with a downsample tier for long-window assertions.

### 7.3 Behavioral assertions

Declarative predicates over a sliding window of metrics + named entity queries, temporal ops `hold / within / eventually / never / converges`. Loaded and polled by the bridge; evaluated in **pure deterministic C++/Python** — an LLM never judges its own Zone (that would poison the signal). The DSL is deliberately capped at metric-field temporal ops + a fixed set of named entity queries; behavior-specific facts (e.g. "wolves pack-hunt within N hours") are emitted as `EventType` records, not derived by a general spatial query language.

### 7.4 Failure-mode watchdogs (always-on)

`extinction` (unexpected pop→0; note true ecological extinction is *allowed* per §3.2, so this watches for *unintended* Zone-wide loss), `explosion` (pop/money > N σ over a calibrated baseline), `deadlock` (no `EntityStateDelta` across K ticks), `runaway feedback` (monotone slope beyond bound), `Director incoherence/oscillation` (intent flip-flop rate, rejected-intent ratio, contradiction with prior plan). Baselines come from a calibrated DETERMINISTIC-NULL soak per level.

### 7.5 Replay & A/B

Replay feeds recorded intent bytes to the validator — bit-exact given the pinned seed + model tier. A/B runs the **same seed + same imported level** under provider-off vs provider-on, diffs metric trajectories, and asserts the Director **adds liveliness** (higher event variety, territory flux) **without breaking guardrails** the baseline held (no explosion/deadlock/oscillation the null run avoided). Same-seed LIVE variance is reported as an envelope band, not a hard pass/fail.

### 7.6 New agent-bridge verbs

`sim{ff|step|pause}`, `snap|restore`, `assert{load|eval}`, `metrics{dump}`, `seed`, `director{mode|replay|inject}`, plus a `tools/zonesoak.py` harness for multi-day headless soaks returning a pass/fail health report. All ride the existing socket/framing; the only engine prerequisite (tick-without-renderer + self-serialize) is already committed. Verbs marshal onto the sim thread (§6.3, L4).

---

## 8. Phasing

**V1 — Director on one region + basic ecology (prove the seam).**
- Region partition authored for the **V1 level set** (a handful of regions, one designated Director focus region).
- Single SoA store, three bands, the §2.5 promotion/demotion with conservation asserts, one clock, fixed-point state, PRNG streams.
- **Basic ecology:** 4–6 species, LV+Holling statistical layer, packs, feeding, carcasses, migration, day/night — *no* deep psi/undead ecology yet; emissions present but simple.
- **Human layer:** faction ledgers, squad FSM, territory control, supply/demand economy, reputation with the single-update relation function (§4.8), emergent quests (a **small template set** — escort, defend, retrieval, avenge), information-latency model (§4.6) in cheap form.
- **Director:** on **one focus region**, full validate/apply two-phase pipeline, memory+reflection, non-blocking + fallback ladder, the full intent schema but exercised narrowly.
- **Content:** new-game-only; hand-authored species eco-param table and job catalogs (do **not** auto-translate EF-storage — treat CoC content as reference, §L3). Canonical post-import snapshot is the test fixture.
- **Testability:** all three run modes, metrics, watchdogs, replay, A/B, `zonesoak.py` — this is how V1 *proves* it works.

**V2 — widen (implied bridge to V3).** Multi-region Director (two-level digest: Zone-overview call picks focus regions, then regional detail), more species and the psi/scavenger/undead roles, scars (§4.10), richer quest templates, emission staggering + catch-up integrator, on-device tactical tier (Haiku/SLM squad orders through the same validator).

**V3 — Zone-wide, deep.** Director steers all ~30–60 regions; full ecology (nutrient cycle, apex territories, rad micro-habitats); deep economy with trade-route arbitrage; long-campaign reflection tuned so feuds persist without context bloat; dormant-tail on-disk paging for very long playthroughs; full information-latency rumor network.

---

## 9. Top risks & mitigations

| # | Risk (from critique) | Mitigation |
|---|---|---|
| **C1** | Four different clocks → replay is a lie | One 100 ms integer tick; all cadences integer multiples in a shared header; wall-clock is a trigger only (§6.1). |
| **C2** | Cohort⇄agent = the dual-model desync reborn | Single-authority rule: agents truth in-bubble, cohort truth outside, never both; conservation asserted at transitions (§2.4, §2.5). |
| **C3** | Fauna + squads + stalkers all claim the same frame, no owner | One global ONLINE admission controller (150–200 total) with priority eviction to OFFLINE (§2.3). |
| **C4** | Floor clamp + Director seeds → LLM-driven boom/bust | Remove floor; true extinction + migration re-colonization; integral-bound cumulative Director magnitude/day; guardian feedback to Director (§3.2, §3.6). |
| **C5** | Materialize-from-counters reinvents ID regen | Ephemeral ids for anonymous fauna; permanent dormant records with stable ids for anything referenced (§2.5). |
| **C6** | Stale ref across async → fatal THROW on Darwin | Two-phase validation; apply-tick liveness re-check; graceful degrade, never throw (§5.3). |
| **H1** | No info propagation → omniscient hive-mind Zone | Event propagation model with per-faction knowledge flags; digest + planners read knowledge-filtered views (§4.6). |
| **H2** | Combat evaporates → no memory of violence | Persistent per-region scar list surviving band demotion (§4.10). |
| **H3** | Named NPC statistically killed off-screen mid-quest | `protected` flag excludes named/story from Lanchester death; quest giver-liveness precondition (§4.3, §4.7). |
| **H4** | Only fauna notice the clock → half-alive world | Shared `daytime` read by squads (night camp), traders (windows), anomalies (time-gated) (§3.5, §4.2). |
| **M1** | Poisson materialize + global cap isn't deterministic | Serialized region-id-ordered materialize pass, budget drawn in order, excluded from parallel phase (§2.5). |
| **M2** | Fixed-point vs float left open = save-format hazard | Fixed-point for all persisted state; floats firewalled to transient physics with a hard assertion (§6.2). |
| **M3** | Emission as synchronized shock → herd/economy resonance | Tick-scheduled staggered emissions, materialization trickle, price-rebound damping (§3.5, §4.5). |
| **M4** | 1/min projection vs fresh-at-fire digest disagree | Digest built fresh at fire time; 1/min is only a max-staleness fallback (§5.1). |
| **M5** | Three writers thrash the relation matrix | One deterministic per-tick update function, fixed fold order; Director sets direction, C++ sets rate (§4.8). |
| **L1** | Region granularity asked in four fragments | Single upstream decision: ~30–60 authored regions, smart-terrains nested (§2.1). |
| **L2** | Legacy save import rabbit hole | New-game-only V1; canonical post-import snapshot as fixture (§8). |
| **L3** | EF-storage `suitable()` translation scope creep | Hand-author eco params + job catalogs; CoC content is reference, not source of truth (§8). |
| **L4** | Bridge main-thread drain vs headless fast-forward | Bridge marshals verbs onto the sim thread; soak drives it directly with drain checkpoints (§6.3). |

---

Full brief also written to `/private/tmp/claude-501/-Users-rz-OpenXVibeRay/e8240705-bcfc-4ad5-95c5-87023c0da9d5/scratchpad/xrsim-brief.md`.

---

# PART II — The Zone Director: hierarchical subagent control

# Zone Director — Hierarchical Subagent Architecture (Design Fragment)

**Scope:** the orchestrator-worker / delegation hierarchy inside the **strategic** and **tactical** tiers of the V1 AI rehaul (Epic A, A3). Extends the roadmap's three-tier model and its hard guardrail: the LLM authors *what the Zone does and why* at the Zone/faction/region level; `xrSim` computes *how it plays out* by fanning intents to thousands of entities. This fragment does not touch the reflex tier's behavior-tree internals (A2) except as the escalation floor.

**One-line thesis:** make complexity cheap by *scoping* it. A frontier model is spent only where scope is broad, stakes are high, and the situation is novel. Everything routine collapses downward — to a small model, a cached decision, or pure deterministic C++. The hierarchy is a **compute-allocation tree**, not an org chart of chatty agents.

---

## 1. The Hierarchy

Five levels. The top three can be LLMs; the bottom two are never LLMs. "Context budget" is the *input* token slice the level is handed; "output budget" is the schema-constrained intent payload it may emit.

| Level | Scope | Cadence (in-game / wall) | Model class | Context budget (in) | Output budget (out) | Count active |
|---|---|---|---|---|---|---|
| **L0 — Zone Director** | Whole Zone as a system: faction balance-of-power, economy macro, emission/anomaly meta-events, narrative causality, migration pressure | every **2–6 in-game hours**; wall ≈ **60–180 s** budget, async | Frontier (Opus, or Sonnet in economy mode) | **6–12k tok**: Zone digest (faction standings, territory map summary, economy indices, recent surprises, active narrative threads) | **≤ 8 directives** (a "Zone plan") + ≤ 1 world-event proposal | **1** |
| **L1 — Faction / Region Subdirector** | One faction *or* one contested region: translate Director directives into a regional plan (which fronts, which smart-terrains, posture, trade policy) | every **20–40 in-game min**; wall ≈ **15–40 s** | Mid/small (Sonnet-small or Haiku) | **2–5k tok**: this faction's slice only — its squads' summary, adjacent enemy summary, its territory, its share of the Zone plan | **≤ 12 regional orders** | **1 per active faction/hot region** — typically **3–6**, hard cap **8** |
| **L2 — Squad / Pack / Site micro-agent** | One unit's next move: a stalker squad's objective, a mutant pack's hunt, a trader's price sheet, a smart-terrain's job assignment | every **2–8 in-game min**; wall ≈ **1–6 s** | Tiny — **on-device SLM** (llama.cpp-Metal, e.g. 1–4B) or Haiku for the batch; many collapse to deterministic | **≤ 800 tok**, usually batched: unit state + its parent order + local situation vector | **1 structured intent** per unit (move/attack/trade/hold/flee) | dozens *considered* per tick, but **~90% collapse to L3**; only novel/surprised ones reach a model |
| **L3 — Deterministic reflex (sim-side)** | Everything mechanical: pathfinding, per-frame movement/aim, needs decay, deterministic order-following, price interpolation, routine job cycling | **per sim tick** (fixed timestep) | **No LLM.** Utility-scored behavior trees + rules in `xrSim`/A2 | full local state (it *is* the sim) | direct state mutation | all entities, always |
| **L4 — Per-frame render/animation** | (out of scope) | per render frame | none | — | — | — |

**Why this shape (refinements from the suggested one):**
- **L1 is dual-keyed on faction *and* region, not just faction.** A contested region (Rostok, the Red Forest) is where conflict actually resolves; keying a subdirector to a hot region lets two factions' plans meet at a seam the Director already flagged. A quiet faction with no active front gets *no* L1 agent — it's steered directly by a Director directive interpreted deterministically.
- **L2 is mostly a router, not a caller.** The default L2 path is deterministic. An L2 *model* call happens only when the deterministic layer reports low confidence / high surprise (Section 2). This is the single most important cost decision in the whole tree.
- **Wildlife packs live at L2 alongside squads** and are steered by an *ecological* subdirector (a specialized L1 whose "faction" is "the wildlife of region R") rather than a human faction subdirector — same machinery, different context template.

---

## 2. Compute Matching — the collapse/escalate rule

Assign model size as a function of four factors, evaluated **per decision, per tick**:

**score = w_scope · Scope + w_stakes · Stakes + w_novelty · Novelty − w_cadence · Cadence**

- **Scope** — breadth of state the decision touches (one unit → 0; a region → mid; the Zone → high).
- **Stakes** — irreversibility × player-visibility. A faction wiped out, a base captured, a named NPC's death → high. A pack picking one of two watering holes → ~0.
- **Novelty** — divergence of the current situation from the last one this agent decided on. Computed deterministically in `xrSim` as a cheap **situation hash + feature-delta** (Section 4): "same inputs as last tick" → 0.
- **Cadence** — how often this fires. High-frequency decisions are *penalized* toward cheaper models, because cost = per-call-cost × frequency and a frequent decision is by definition low-marginal-value.

**Routing bands (ballpark thresholds, tuned in A4):**
- score < 0.2 → **L3 deterministic** (no LLM). This is the target for the overwhelming majority of L2 decisions.
- 0.2–0.5 → **on-device SLM / Haiku**, batched (L2/L1 small).
- 0.5–0.8 → **Sonnet** (L1, or an L2 escalation).
- > 0.8, or an explicit surprise flag → **Opus** (L0, or a rare L2→L0 escalation for a genuinely Zone-shaking event like an unscripted base fall).

**Collapse (the default direction):** every level runs its deterministic version *first* and only escalates on low confidence or high novelty — a **model cascade** in the FrugalGPT sense (cheap-first, escalate on low confidence, matching top-model quality at a fraction of the cost). "Believability adds nothing here" is the collapse test: if a deterministic rule produces a result a player can't distinguish from an LLM's, use the rule. Traders restocking on a schedule, a pack returning to a known den, a squad marching a Director-ordered route — all deterministic.

**Escalate (rare, event-driven):** escalation is triggered by *surprise*, not by a clock. Triggers: a plan precondition broke (ordered-to route now blocked by a new anomaly), a stakes threshold crossed (faction about to be eliminated), player action outside the model's last plan (player wiped a squad the Director assumed alive), or an L1/L2 self-reported low-confidence intent. Surprise is detected deterministically in `xrSim`; the LLM is woken *by the sim*, never polling.

---

## 3. Delegation Mechanics

**Context minimization (the core discipline).** Each child is spawned with a **self-contained task packet** and a **fresh context** — it does not see siblings, the Director's full reasoning, or global state. This is Anthropic's orchestrator-worker isolation bet ("subagents know almost nothing about each other") applied to a game: it's what lets subdirectors run in true parallel and keeps the parent's context from drowning. A packet has exactly four fields (mirroring the "objective / output format / tools / boundaries" rule that fixed duplicated-work failures in the research system):

1. **Objective** — the parent directive this child owns ("hold Rostok's south gate; push toward the wild-territory corridor").
2. **Scoped state slice** — only what this child needs, rendered by `xrSim` as a compact digest: for an L1, its faction's squads + adjacent enemies + its territory; *not* the whole Zone.
3. **Output schema** — the exact intent grammar it may emit (a JSON schema / tool definition; invalid output is a value the validator rejects, not a throw — fits `XRAY_EXCEPTIONS=0`).
4. **Boundaries** — hard limits: token budget, which regions/units it may address, and "you may not contradict directive D".

**Return path.** Children return **schema-constrained intents only** — never prose, never state mutations. The parent (or `xrSim` directly, for L2) validates against the schema, then against sim legality (does this squad exist? is this move reachable? is the trade solvent?). Illegal or budget-overrun intents are dropped and logged; the unit coasts on its prior intent.

**Aggregation → xrSim intents.** The parent does **not** re-reason over children's outputs (that would re-inflate its context). It performs a **deterministic merge**: collect child intents, resolve conflicts by fixed precedence (Section 5), stamp each with `{tick, agent_id, parent_directive_id}` for replay, and hand the merged intent batch to `xrSim`'s validator, which fans each intent to the affected entities. The LLM tree's total output for a whole strategic tick is a few dozen structured intents — kilobytes — against a sim state of thousands of entities.

**Concurrency & bounding.** L1 subdirectors run **in parallel** (independent contexts, independent HTTP/SSE calls). Concurrency is capped two ways: a **fan-out cap** (≤ 8 L1 agents, ≤ N L2 model-batches per tick) and a **token/compute budget** (Section 4) that can shrink the fan-out mid-tick. Parallelism is where the latency win lives — the whole L1 layer resolves in one wall-clock subdirector window, not serially — but the parent waits for the batch (a known synchronous-bottleneck tradeoff we accept because the game never blocks on it anyway: a late batch just misses this tick).

---

## 4. Economy of Compute

The budget is the spine. Everything here serves **a per-tick global compute budget with graceful degradation.**

**Per-tick global budget.** A configurable ceiling in LTX, e.g. **strategic tick ≤ 120k input + 20k output tokens** (subscription-first: expressed as a token/latency budget, mapped to plan limits). `xrSim` tracks spend as it dispatches. When the budget is exhausted mid-tick, remaining work degrades in a **fixed skip order**:
1. Skip L2 model calls (they were already mostly deterministic) → those units coast on deterministic reflex.
2. Skip cold/quiet L1 subdirectors → those factions coast on their last regional plan.
3. Never skip L0's *application* of an already-computed plan; only skip *recomputing* it (the world coasts on the last Zone plan — exactly the roadmap's "missed tick" contract).
So under pressure, the Zone degrades gracefully from "LLM-alive" → "coasting on last plan" → "pure deterministic sim," never to "stalled."

**Batching.** The default is **many units in one call**, not one-per-call. An L1 subdirector plans *all* its squads in a single call (the roadmap's fan-out-is-the-sim's-job principle). At L2, when models are needed, the router **groups surprised units of the same kind** (all confused stalker squads in region R; all disturbed packs) into one batched call with an array output — one call decides 10 squads. One-per-call is reserved only for a single high-stakes named entity. (Batching is why token cost stays sane: the research-system finding is that multi-agent burns ~15× a chat's tokens; we counter that by making most "agents" deterministic and batching the rest.)

**Caching + reuse (FrugalGPT-style).** Two caches:
- **Decision cache**, keyed on the deterministic **situation hash** (Section 2's novelty input). Same inputs as a recent decision → replay the cached intent, zero tokens. This is what makes "routine" free.
- **Prompt-prefix cache** for the stable parts of each level's context template (the schema, the boundaries, the Zone-digest scaffold) so only the volatile slice is fresh tokens.

**Escalation only on novelty/surprise** (Section 2) — the clock never *forces* an LLM call; it only *permits* one. A perfectly stable Zone hour can cost near-zero tokens.

**On-device vs cloud routing.**
- **On-device (llama.cpp-Metal SLM):** L2 batches, tactical dialogue, anything latency-sensitive and low-stakes. Free (no API cost), private, always-available — this is the local-fallback tier from the roadmap. Unified memory makes a 1–4B model cheap to keep resident.
- **Cloud (HTTP/SSE):** L0 always; L1 when score > 0.5. If cloud is unavailable or over budget, L1 falls back to the on-device model at reduced quality, and L0 falls back to *not recomputing* (coast). Routing is a pure function of (score, budget-remaining, provider-availability) computed in `xrSim`, so it's deterministic and replayable.

---

## 5. Failure / Coherence

**Contradiction avoidance (children vs parent, children vs each other).**
- **Directive as contract, not suggestion.** A child's boundaries include "may not contradict directive D." The validator enforces it structurally: an L1 order that violates its parent directive is *rejected at merge*, not blended. Coherence is enforced by the schema and precedence rules, not by hoping the models agree.
- **Fixed conflict precedence** at merge: L0 directive > L1 order > L2 intent; for equal-level conflicts (two L1s claim the same smart-terrain), a deterministic tiebreak (faction priority, then agent_id) decides. `xrSim` is the single arbiter; two children can *propose* conflicting moves, but only one legal outcome is applied.

**Oscillation & drift.**
- **Hysteresis / commitment windows:** an intent, once applied, holds for a minimum dwell time before it can be reversed, so a squad can't flip between "advance/retreat" every tick because two ticks scored marginally differently.
- **Drift control:** L1/L2 plans have explicit **expiry**; on expiry they don't vanish, they fall back to the last L0 directive interpreted deterministically. The Director's slow tick is the anchor that keeps fast agents from wandering. Zone/faction memory is summarized (reflection) rather than accumulated raw, bounding context drift over long sessions.

**Dead/slow subagent → deterministic degradation.** Every LLM level has a **deterministic shadow** that can produce a valid (duller) intent for the same packet:
- L2 dead → utility-scored behavior tree (A2) picks the move.
- L1 dead/timed-out → last regional plan continues; if none, units follow the raw L0 directive via a rule interpreter.
- L0 dead → the whole Zone coasts on the last Zone plan indefinitely; the game is fully playable with **zero** LLM availability (the roadmap's "local fallback" promise). A timeout is a normal, expected value, never an error.

**Determinism / replay with a *dynamic* agent tree.** The tree's *shape* changes tick to tick (which L1s exist, which L2s escalated) — this must not break replay. Achieved by making **every agent-tree decision a logged, deterministic function of sim state + a seeded RNG + recorded LLM responses:**
- The **routing decision** (spawn which agents, which model, collapse or escalate) is a pure function of the snapshot + budget + a per-tick seed → replayable exactly.
- **LLM responses are recorded** (request packet + response intent, keyed by agent_id + tick) behind the recordable interface (A4). In replay mode, the recorded response is served instead of a live call — so the non-deterministic LLM is turned into deterministic input. The sim's *application* of intents was always deterministic; recording the intents makes the whole tree replayable.
- The dynamic tree is therefore just data: a per-tick list of `{agent_id, parent, model, packet_hash, response}` that reconstructs identically from the same snapshot + recording.

---

## 6. Concrete Example — one strategic tick, end to end

Setup: in-game 14:00. Duty holds Rostok; Freedom pressures the wild-territory corridor to the west; a blind-dog and boar population sits between them; player is somewhere near Rostok.

**t0 — L0 Zone Director (Opus, cloud, ~9k tok in).** `xrSim` builds the Zone digest: faction standings, a coarse territory map, economy indices, and a **surprise list** (this tick: "player wiped a Freedom recon squad north of Rostok" — flagged high-novelty). Director reasons about balance-of-power and emits a Zone plan of 3 directives, among them:
> **D1:** `{faction: Duty, intent: PUSH, target_region: Rostok→wild_territory_corridor, posture: aggressive, horizon: 4h}`

Output: ~600 tokens of structured directives + `agent_id=L0, tick=T`.

**t1 — L1 fan-out (parallel).** Router scores each faction/region. Duty-push scores high (broad scope, high stakes, novel because of the player kill) → **Sonnet, cloud**. Freedom scores mid → **Haiku**. The wildlife of the corridor scores low but non-zero (human activity about to shift) → **on-device SLM ecological subdirector**. Three packets, three parallel calls, each with only its slice:

- **Duty subdirector** gets: D1, Duty's 5 squads' summaries, adjacent Freedom force estimate, the corridor terrain digest, boundary "don't contradict D1." Returns ~10 regional orders:
  > `O1: squad_Duty_3 → seize checkpoint C7`; `O2: squad_Duty_1 → screen north flank (player last seen)`; `O3: reinforce Rostok gate`; …
- **Freedom subdirector** (Haiku) gets its own slice; reacts to pressure with a fighting-withdrawal plan for the corridor.
- **Ecological subdirector** (on-device) gets: corridor wildlife summary + "human military activity rising in corridor." Returns a posture: `packs → displace toward Red Forest den, avoid corridor`.

**t2 — L2 (mostly deterministic, one batched model call).** `xrSim` expands each L1 order to its units. Most collapse to L3: squad_Duty_3's move to C7 is just pathfinding + a march intent — **no LLM**. But squad_Duty_1's "screen the flank where the player killed our recon" is flagged novel (the player is an unmodeled actor) → escalates. The router **batches** the 2 flagged Duty squads + 1 flagged blind-dog pack (disturbed by the shift) into a single **on-device SLM** call with an array output:
> `squad_Duty_1: advance-by-bounds to grid G, weapons hot`; `squad_Duty_4: hold overwatch`; `pack_blinddog_2: relocate to den D, hunt en route`.

**t3 — merge → xrSim intents.** Parents deterministically merge child intents, resolve one conflict (two Duty orders both wanted squad_Duty_3 — precedence keeps O1, drops the duplicate), stamp each intent `{tick=T, agent_id, parent=D1}`, and hand the batch to `xrSim`'s validator. Validator checks existence/reachability/solvency, then fans out: squads get movement + posture goals (executed frame-by-frame by L3 behavior trees + pathfinding), the trader in Rostok gets a price bump on medkits (deterministic economy rule triggered by "aggressive posture" → higher demand), packs get new territory targets.

**Net for the tick:** 1 Opus call, 2 cloud small calls, 1 on-device batch — ~15k input / ~2k output tokens total for a Zone-wide plan that then plays out mechanically over the next 4 in-game hours with **zero further LLM calls** unless a new surprise fires. What ran where: Opus = Zone strategy; Sonnet/Haiku = per-faction regional plans; on-device SLM = the few genuinely-novel unit decisions + wildlife; deterministic C++ = everything else and all execution.

---

## 7. Testability via the Agent Bridge

The existing bridge (verbs `hello/cmd/lua/state/shot`, newline-framed over the Unix socket, `agentctl.py`, and the planned fixed-timestep replay paired with `xrSim` snapshots) is extended with an **AI-orchestration inspection + record/replay surface**. New verbs (same framing, same request-ID pipelining):

| Verb | Payload | Returns |
|---|---|---|
| `ai.snapshot` | — | serialize sim + full agent-tree state for a tick (deterministic seed included) |
| `ai.tree` | `<tick>` | the reconstructed agent tree for a tick: `[{agent_id, level, parent, model, packet_hash, budget_spent}]` |
| `ai.record` | `on/off <path>` | toggle recording of every `{packet, response}` behind A4's recordable interface |
| `ai.replay` | `<recording>` | feed recorded LLM responses instead of live calls → deterministic re-run |
| `ai.budget` | `<tick>` | per-tick token/compute accounting: spend per level, skips, degradation events |
| `ai.inject` | `<intent-json>` | inject a schema-valid intent to test the validator/fan-out in isolation |
| `ai.provider` | `on/off/local` | force provider-off or on-device-only to test fallback correctness |

**Per-level assertions** (bridge-scripted, extending A4's behavioral tests):
- **L0:** "given surprise=player-killed-recon, Director's plan contains a directive touching that region within 1 tick."
- **L1:** "the Duty subdirector's orders never contradict directive D1" (schema-check every order against its parent — a *coherence* assertion).
- **L2:** "≥ 90% of squad decisions this tick collapsed to deterministic (0 tokens)" (a *compute-matching* assertion — proves complexity stayed cheap).
- **Cross-level:** "no two applied intents target the same smart-terrain" (conflict-resolution assertion).

**Budget accounting.** `ai.budget` gives a hard number per tick; a regression test asserts a scenario stays under its LTX ceiling and that degradation fired in the correct skip order when the budget was throttled (via `ai.provider`/a lowered ceiling).

**Replay.** `ai.record` a live session → `ai.replay` it → assert byte-identical sim snapshots at each tick. This proves the *dynamic agent tree is deterministic under replay* (Section 5): same snapshot + same recorded responses + same seed → same tree shape → same outcome. This is the acceptance gate for the whole subsystem, and it runs headless in CI-style bridge scripts.

---

## Data / interface additions `xrSim` needs

- **Situation hash + feature-delta** per decision unit (squad/pack/faction/region) — the deterministic novelty/cache key. Cheap, computed every tick.
- **Directive / Order / Intent record types** with `{id, tick, agent_id, parent_id, schema_version, payload}` — the audit trail and replay backbone; also the merge/precedence unit.
- **Intent validator + fan-out** — the single choke point: schema-check → sim-legality-check → apply-to-entities. Rejections are values (no throw), logged.
- **Routing function** `route(snapshot, budget, provider_state, seed) → agent_tree_plan` — pure, replayable; owns collapse/escalate and model selection.
- **Budget ledger** — per-tick token/compute spend, skip log, degradation events; surfaced via `ai.budget`.
- **Context-slice renderers** — per-level templates that emit the compact scoped digest (Zone digest for L0, faction slice for L1, unit+order for L2) from sim state. These *are* the context-minimization mechanism.
- **Recordable provider interface** — `{packet → response}` behind a record/replay shim (already anticipated by A4); the seam that turns non-deterministic LLMs into deterministic replay input.
- **Deterministic shadow** for each LLM level — the fallback intent generator (behavior-tree / rule interpreter) invoked on dead/slow/over-budget.

## Open questions

1. **Novelty metric fidelity.** How rich must the situation-hash feature vector be before "novel" correlates with "a player would notice"? Too coarse → visible repetition; too fine → cache never hits, cost blows up. Needs A4 tuning against real playthroughs.
2. **On-device model size vs quality floor.** Is a 1–4B llama.cpp-Metal model good enough for L2 batches and L1 fallback, or does believability demand ~7–8B? Trades unified-memory footprint against fallback quality — measure before committing.
3. **L1 keying.** Faction-keyed vs region-keyed vs the dual key proposed here — does the dual key cause double-planning at faction/region seams, and is the precedence tiebreak enough, or is an explicit "seam owner" needed?
4. **Commitment-window length.** Too long → the Zone feels rigid and slow to react to the player; too short → oscillation. Likely per-decision-type, tuned empirically.
5. **Subscription budget mapping.** The roadmap is subscription-first; how does a token/latency budget map onto plan rate limits, and what's the graceful-degradation behavior specifically when a *plan* (not a token budget) is the binding constraint?
6. **Batched-call blast radius.** One batched L2 call deciding 10 squads means one bad/malformed response degrades 10 units at once. Is per-unit fallback within a batch enough, or should high-stakes units always be un-batched?

## Sources

Multi-agent orchestration and delegation:
- [How we built our multi-agent research system — Anthropic](https://www.anthropic.com/engineering/multi-agent-research-system) (orchestrator-worker pattern, context isolation, objective/format/tools/boundaries delegation, effort-scaling rules, ~15× token cost, parallel subagents, checkpoint/resume, when-not-to-use)
- [How Anthropic Built a Multi-Agent Research System — ByteByteGo](https://blog.bytebytego.com/p/how-anthropic-built-a-multi-agent)

Hierarchical planning for game agents:
- [Offline Planning with Hierarchical Task Networks in Video Games — Kelly et al. (AIIDE'08)](https://www.eecs.ucf.edu/~gitars/cap6671/Papers/AIIDE08-010.pdf)
- [Advanced Real-Time Hierarchical Task Network — AAAI/AIIDE](https://ojs.aaai.org/index.php/AIIDE/article/download/18910/18675/22676)
- [Hierarchical Control in Multi-Agent Games: LLM-based Planning and RL Execution](https://arxiv.org/html/2606.20014) (LLM plans, cheaper layer executes)

Model cascades / cost-aware routing (compute matching + economy):
- [FrugalGPT: Reducing LLM Costs While Improving Performance](https://arxiv.org/pdf/2305.05176) (cheap-first cascade, escalate on low confidence, semantic caching, up to ~98% cost reduction at matched quality)
- [LLM Routing and Model Cascades — TianPan.co](https://tianpan.co/blog/2025-11-03-llm-routing-model-cascades) (45–85% cost cut at ~95% quality)
- [Cluster, Route, Escalate: Cascaded Framework for Cost-Aware LLM Serving](https://arxiv.org/html/2606.27457)

---

**Summary of what this design commits to:** a 5-level compute-allocation tree (Zone Director → faction/region subdirectors → squad/pack/site micro-agents → deterministic reflex → render), where model size is a per-decision function of scope × stakes × novelty ÷ cadence; the default direction is *collapse to deterministic*, escalation is *surprise-driven not clock-driven*, children get minimized context packets and return schema-only intents that `xrSim` deterministically merges and fans out; a per-tick token budget degrades gracefully in a fixed skip order down to a fully-playable zero-LLM sim; coherence is enforced structurally (directive-as-contract + fixed precedence + hysteresis) rather than by hoping models agree; and the whole dynamic tree is made replayable by recording LLM responses behind the agent-bridge's record/replay seam, with per-level bridge assertions for coherence, compute-matching, and budget. It respects the hard line — the frontier model is spent once per several in-game hours on the Zone as a whole, and almost nowhere else.
