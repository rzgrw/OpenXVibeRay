# Seamless Whole-Zone Streaming (xrStream) — Design Spec

**Date:** 2026-07-05
**Status:** Design approved in direction (rz) — implementation spec. Couples the renderer (V1 Epic R) and the sim (V1 Epic A).
**Parents:** `2026-07-05-zone-simulation-design.md` (xrSim), `2026-07-04-engine-v2-roadmap.md` (native Metal renderer)
**Provenance:** 7-agent design workflow (X-Ray level structure grounded in this repo's source, streaming architecture, sim coupling, renderer residency, memory/feasibility) + gap-critique + synthesis. Fragments in the session scratchpad.

## The one-paragraph takeaway

X-Ray **already** simulates the whole Zone continuously and **already** has a global world coordinate frame (`SLevel::m_offset`, `tGlobalPoint` — `game_graph_space.h:35,77`); ALife buckets every object across every level. The loading screen exists purely because four artifacts are baked per-level and swapped monolithically: render geometry, the single collision CDB (`CObjectSpace::Static`), HOM occlusion, and the single-level AI graph. So a seamless Zone is a **de-singleton-izing + streaming problem, not a content-recompile problem** — we stream abutting pre-baked level-cells in the existing global frame. A full "one giant map" recompile is a verified trap (it overflows u16 graph/sector ids, u8 level id, and the 24-bit nav-node budget at once) and is rejected. **Honest target:** "no loading" is fully achievable within a physically-contiguous cluster of cells; elsewhere it degrades openly to sub-second async transitions. The whole Zone is always *simulated*; what streams is how much you can *see and touch*.

---

Seamless Whole-Zone Streaming — Design Brief

  

    
Design Brief · OpenXVibeRay · xrStream
    
# Seamless Whole-Zone Streaming on the X-Ray engine
    
One continuous, always-simulated S.T.A.L.K.E.R. Zone — streamed as a quilt of pre-baked level-cells on unified-memory Apple silicon, without a runtime world recompile.
    

      Target: M-series / Metal R1→R2
      Sim spine: xrSim fixed-tick authority
      Status: pre-spec synthesis
      Constraint: XRAY_EXCEPTIONS=0 (Darwin)
    
  

  
01
## Verdict, stated plainly

  
The architecture the fragments converge on is correct and its enabling facts are verified in-repo: a global world frame already exists (SLevel::m_offset, tGlobalPoint at game_graph_space.h:35,77); the blocking singletons are real; the full-recompile "one giant map" path is a genuine trap (it overflows u16 _GRAPH_ID, u8 _LEVEL_ID, the 24-bit node budget, and the single-Static-CDB assumption). We adopt it. But we refuse the framing that "seamless" is uniform.

  

    
The honest target
    
"Load the whole map without loading" is fully achievable only within a physically-contiguous cluster of cells. Everywhere else it degrades — deliberately and openly — to sub-second async cell transitions. The whole Zone is always simulated; what streams is only how much of it you can see and physically touch.
  

  
STALKER is per-level-baked by construction, and real Call of Chernobyl seam topology is a mix of walk-through adjacencies and teleport transitions (surface↔underground, Pripyat↔Jupiter). So we ship three honest tiers, not one promise:

  

    

      CLASS A
      
Hitchless within a clusterwalk-through seam, both cells resident
      
Abutting cells placed edge-to-edge by graph offset, portal-stitched at the old transition volume. Live crossing, id-stable, no screen. This is the real deliverable.
    
    

      CLASS B
      
Fast async at cluster boundariesoverlapping / stacked offsets
      
Cells whose baked offsets don't tile cleanly. A one-time authored re-layout promotes some to Class A; the rest take a masked ~100–400ms async handoff.
    
    

      CLASS C
      
Sub-second masked transitionteleport / non-contiguous seam
      
Physically disconnected links. These cannot be zero-screen — the destination isn't spatially where you'd walk. Best case is a masked async load. We say so.
    
    

      ALWAYS
      
Statistical sim, Zone-wide, everywhereindependent of residency
      
Factions, wildlife, economy, emissions and events run continuously in every region whether or not its geometry is loaded. Streaming never gates simulation fidelity.
    
  

  

    Blocking unknown — resolve before any code
    
The res/gamedata tree in this repo is empty of baked artifacts — no level.cform, no game.graph, no .gct. Every number below for per-cell footprint, OPCODE build time, and the A/B/C split is an engineering estimate. The first deliverable is not the residency manager — it is a 200-line offline tool that parses a real CoC game.graph + all.spawn, dumps every SLevel::m_offset and level AABB, and classifies all ~30 transitions A/B/C. That single artifact decides the scope of the entire feature.
  

  
02
## X-Ray reality: what's baked, and how we work around it

  
Six artifacts are compiled offline per level and today swapped wholesale at a transition — which is exactly why the loading screen exists. The decision, argued below, is to stream abutting baked cells (a cell = one imported CoC level, the natural bake boundary) and reject the full-world recompile.

  

  
    
Baked artifactToday (singleton)Streamed-cell approachRepo anchor
    
      
 | Render geometry
VB/IB, visuals | One monolithic set per $level$, loaded all-at-once | Per-cell sub-dsgraph; VB/IB in a per-cell placement heap | r2_loader.cpp:17
      
 | Collision
level.cform | One global CDB::MODEL Static, hard-coded path | map<cell_id, CDB::MODEL>; queries fan out to overlapping cells | xr_area.cpp:93
      
 | Sectors + Portals | Flat per-level Sectors/Portals vectors | Global registry, cell-scoped ids; new seam portals stitch cells | r__sector.h
      
 | HOM occluder | One baked occluder mesh per level | Kept as intra-cell occluder; add coarse per-cell hull + Zone occluder | HOM.h
      
 | AI navigation
level.ai | Exactly ONE CLevelGraph resident; fatal invariant | N resident graphs keyed by level; clamp-not-abort promotion | AISpaceBase.cpp:17
      
 | Game connectivity
game.graph | Already one unified world space | Adopt as-is — the frame we stream into. No rebuild. | game_graph_space.h:35
    
  
  

  

    The load-bearing insight
    
The whole-Zone sim and the global coordinate system already exist. ALife already buckets every object by game-vertex across all levels and runs off-level objects OFFLINE. The loading screen exists purely because RENDER, COLLISION, HOM, and the SINGLE AI graph are baked per level and swapped monolithically. Making it seamless is a de-singleton-izing problem, not a content-recompile problem.
  

  
### Why not recompile one giant map
  
A merged world would overflow four hard budgets simultaneously — the u16 game-graph id (65,536 vertices Zone-wide), the u8 level id (256), the 24-bit per-level nav node link budget, and the u16 per-level sector/portal ids — and would still require re-baking lighting, CDB, sectors and the AI graph across 30+ levels. That is a content-pipeline project, not a runtime one. Decision: stream abutting cells; keep every baked artifact per-cell; stitch at seams. A hybrid touch — pre-merging tiny connective corridors into their larger neighbor at bake time — reduces seam count cheaply.

  
03
## World model: unified space, cells, regions

  

    

      
#### Unified coordinate spaceadopt, don't recompute
      
The frame is the pre-baked game-graph offset frame. Geometry stays level-local; each cell carries a world transform (SLevel::m_offset) applied at query/draw time. This preserves baked float precision and avoids far-from-origin blowup — cells never re-authored into absolute coordinates.
    
    

      
#### The cell= one imported CoC level
      
Every baked artifact partitions on the level boundary, so the level is the cell — the natural granularity, ~30+ for CoC. Sub-tiling oversized levels is deferred to V3 (it means re-cutting bakes). A cell owns its CDB island, sub-dsgraph, nav slice, detail slots, GPU resource set, and a generation counter.
    
    

      
#### Region mappingsim ↔ render coherence
      
xrSim's ~30–60 authored regions are the addressing layer (they respect the id caps). A region aggregates one or more render cells. Region adjacency + game-graph cross-edges drive both residency and sim LOD, so what streams and what simulates stay coherent by construction.
    
    

      
#### ID disciplinenever flatten
      
Cross-level edges live in cross-tables (level.gct), never a merged mega-graph. Entity truth is xrSim's stable u32 id in the global SoA store — never a per-level or per-cell id. Crossing a seam changes an entity's graph vertex, never its identity.
    
  

  
04
## The streaming system (xrStream)

  
### Per-cell state machine
  
Every cell is built entirely off to the side on a worker thread and touches no global state until a single main-thread atomic publish. The registry only ever references fully-built cells, so render/collision/query traversal can never observe a half-loaded cell — the invariant that prevents a dangling reference from becoming a fatal VERIFY abort on Darwin.

  

    

      NOT_RESIDENT→
      LOAD_QUEUED→
      LOADING→
      READY_TO_PUBLISH→
      RESIDENT→
      EVICT_QUEUED→
      UNLOADING→
      NOT_RESIDENT
    
    
LOADING (worker): FS read → decompress → CDB::MODEL::build → sub-dsgraph → nav slice → GPU upload into MTLStorageModeShared. Nav slice loads FIRST (cheapest; keeps OFFLINE agents pathing across a not-yet-rendered seam).  ·  PUBLISH (main, ≤1 cell/frame, budgeted): generation bump + registry pointer swap + spatial/sector inserts.
  

  
### Hysteresis rings + the seam-arrival guarantee
  
Residency tier is derived from cross-level-edge distance to the player's cell (with a euclidean fallback), not raw distance alone — the game graph already encodes which levels abut. Rings are asymmetric (load at R, unload at R+margin) to kill boundary thrash, reusing X-Ray's own switch_distance·(1±factor) idiom. The critique's cadence conflict is resolved by decoupling two clocks:

  

    

      
#### Slow clock — steady-state residency1 Hz coarse re-eval
      
Recomputes the resident set + LOD bands from player region and Director focus. Fine for background paging and eviction.
    
    

      
#### Fast clock — seam-proximity prefetchcontinuous, event-driven
      
The instant the player enters a cell that owns a Class-A seam, the neighbor load is kicked unconditionally — it does not wait for the next 1 Hz tick. Horizon is sized from measured worst-cell load time × max player speed (~6 m/s sprint). Until measured, the worst seam is treated as not-hitchless and masked.
    
  

  

    
      
        
          
          
        
      
      
      
      
      
      
      
      
      
      
      
      player
      FULL · render+CDB+nav+detail
      PROXY · hull+box+full nav
      STATISTICAL · aggregate math only
      
      
- 
      
- 
      Class-A seam
      
      
      neighbor cell
      prefetched on cell-enter
      

      

    
    
Residency tiers mirror xrSim's ONLINE / OFFLINE / STATISTICAL bands. The fast clock fires the neighbor prefetch on cell-enter, so a sprinting player always has a Class-A seam's far side resident before arrival.
  

  
### Seam continuity, by domain
  

  
    
DomainMechanismCorrectness note
    
      
 | Geometry / render | Adjacent resident cells render together; authored transition volume becomes a cosmetic fog/skirt band masking any tile gap | Only truthful for Class-A seams (see Risk P0-2)
      
 | Collision | Each cell keeps its own CDB island; ray/box/nearest queries fan out to cells whose world AABB the query overlaps (1–3) | Result must carry (cell_id, element) — see Risk P1-4
      
 | Navigation | Per-cell level graphs stitched at pre-authored cross-table edges; OFFLINE agents path across through existing edges | No invented stitching, no teleport
      
 | Occlusion | Per-cell HOM for intra-cell; new seam portals + coarse per-cell-hull Zone occluder cull whole distant cells | Cross-seam culling is conservative; over-draw at open seams (Risk P2-8)
    
  
  

  

    Determinism firewall
    
Residency is a pure function of ideal sim state (player region + Director focus at sim_time) — never of I/O completion timing. The sim always simulates against the ideal resident set; render/physics pop in late against the actual set. If I/O lags a seam crossing, OFFLINE agents keep pathing on the nav slice and physics arrives a few ticks late — a fidelity delay, never a divergence. Saves store ideal residency; actual is reconstructed on load.
  

  
05
## Sim coupling: bands, handoff, cross-region interactions

  
### The band ↔ residency lattice — one gate
  
The entire coupling contract is a single clamping function that replaces the historical fatal assert:

  

    The coupling function
    
admissible_band(e) = min( desired_band(e), residency_ceiling(e.pos_region) )
  

  

  
    
BandRequires residentCadenceWhat it is
    
      
 | DORMANT / STATISTICAL | nothing | 0.1 Hz | Pure graph / aggregate math — authoritative
      
 | OFFLINE | game-graph + AI-map | 2 Hz | Graph-vertex movement; no physics, no draw
      
 | ONLINE | full CFORM + level.ai + sectors + BLAS | 30 Hz | Physics + animation + reflex trees
    
  
  

  
Today, alife_switch_manager.cpp asserts a switching object's graph level equals the loaded level — a fatal abort on Darwin (XRAY_EXCEPTIONS=0). We replace every such invariant (at :51, :136, :195 — the critique found more than one) with clamp-and-continue: if geometry isn't resident, the entity is capped at OFFLINE, never promoted, no throw.

  
### Seam handoff — conservation, no id regen
  
    
- Entities are never owned by a cell. xrSim's global SoA store owns them by stable u32 id + region. Crossing a seam changes nothing about ownership — this kills the classic A-Life dup/loss bug at its root.
    
- OFFLINE cross: reassign only m_tGraphID across the cross-level edge. Same record, same id, proxy-less if the destination isn't resident.
    
- ONLINE live cross (player on a seam): both cells are resident; detach the physics/render proxy from cell A, attach to cell B at the shared portal vertex, carry the transform — atomically inside one step_sim tick, between two rendered frames. No half-in-half-out frame.
    
- Conservation ledger asserted per handoff as a logged value (never THROW): sum(live + dormant + statistical) per faction/species conserved modulo logged births/deaths.
  

  
### Cross-region interactions run regardless of what's streamed
  
This is the whole point of "the Zone is always alive," and it is nearly free in memory (~10–30 MB for the entire statistical layer — a rounding error against textures).
  

    

#### Faction squads
A squad marching A→B→C is an FSM in TRAVELING running HPA* over the region graph. It advances every OFFLINE tick whether or not B is loaded; it materializes to ONLINE NPCs only inside the player's resident ring.
    

#### Wildlife migration
A cohort fraction diffusing along a region-adjacency edge per ECO_TICK. Migrating across an unloaded boundary is ordinary aggregate math — the seam is just a graph edge.
    

#### Emissions & events
An emission sweeping regions fires per-region aftermath on a deterministic staggered schedule regardless of residency; only resident regions render VFX / materialize casualties. Events spread under the information-latency model along squad line-of-contact.
    

#### Economy
Goods flow along controlled trade routes on the region graph with zero residency dependence. Residency changes what you can see and touch — never interaction fidelity.
  

  
06
## Renderer & residency on the Metal R1/R2 path

  
Residency maps 1:1 onto Metal primitives, and unified memory is the enabler: MTLStorageModeShared means the streaming thread memcpys decoded geometry/textures straight into the heap-backed resource the GPU reads — no staging blit, no didModifyRange, killing the current Map/Unmap upload stall at cell load.

  
    
- Per-cell MTLResidencySet. Promote = addAllocation + commandQueue.addResidencySet; evict = remove + heap-region free. Replaces the monolithic level_Unload teardown with per-cell add/remove.
    
- Bindless world descriptor table (argument buffer tier 2) indexes every possibly-visible resource. Cell promote writes its slot range; evict clears it. The GPU indexes by cell/material id — no per-cell CPU binding.
    
- Placement heap (geometry) + sparse heap (textures). Each cell is one contiguous heap sub-region freed as a unit — no fragmentation. Sparse MTLTextures stream mip tiles by GPU-feedback, with an always-resident low-mip tail so a suddenly-visible surface is blurry-correct, never a hole.
    
- Residency-change-only ICB rebuild. One indirect command buffer encodes all resident cells' draws, culled on-GPU against the coarse occluder + frustum. Rebuilt (double-buffered, atomic swap at frame boundary) only when the resident set changes; reused verbatim otherwise — zero per-frame allocation, satisfying the R2 exit criterion.
    
- Cell-band geometry LOD. Near = full dsgraph; mid = coarse hull + FLOD impostors (the existing 8-facet billboard); far = one baked impostor card per cell. Lets the player see across the contiguous cluster at bounded cost.
    
- Frame-boundary atomic publish. A cell becomes referenceable only when its residency set, descriptor slots, and ICB entry are all complete — a half-loaded cell is never indexed. Stability-critical, not a nicety, on XRAY_EXCEPTIONS=0.
  

  
07
## Memory & performance budget

  
The binding constraint is not the sim — it is textures and the baked seams. One dense cell's estimated resident footprint (unmeasured; confirm against real gamedata before locking ring sizes):

  

  
    
ComponentPer dense cell (est.)Note
    
      
 | Textures | 200–600 MB | Dominates; shared via registry + global mip LRU
      
 | Render geometry + VBs | 150–400 MB | Per-cell placement heap
      
 | Collision (xrCDB / OPCODE) | 30–80 MB | Build is the one CPU-heavy load step — time-slice it
      
 | Nav + graph slice | 10–40 MB | Loads first
      
 | Sound / misc | ~50 MB | 
      
 | Full cell total | ~0.6–1.4 GB | = what one CoC level costs to load today
      
 | Whole-Zone sim state | ~10–30 MB | SoA ~40–60 B/entity; the biggest win — "alive" is nearly free
    
  
  

  

  
    
Mac tierResident setRing radiusClass-A live crossingWorking set
    
      
 | 16 GB | 1 hot + 1 ring | 1 hop | disabled | ~2–3 GB
      
 | 24–32 GB | 1 hot + 2–4 ring | 1–2 hops | yes | ~4–6 GB
      
 | 64 GB | 1 hot + 4–6 ring | 2 hops | yes | ~6–9 GB
    
  
  

  

    16 GB is a fast-transition tier, not a seamless one
    
MTLStorageModeShared allocations count fully against unified-memory pressure and are not swappable like private VRAM — under pressure the app is killed by jetsam, not slowed. Once OS/WindowServer baseline (~3–4 GB), driver metadata, and any RT acceleration structures are added, 16 GB has no room for co-resident seam crossing. On 16 GB: hard-cap to 1 hot + 1 ring, disable RT, register a DISPATCH_SOURCE_TYPE_MEMORYPRESSURE handler that force-evicts the ring cell before jetsam fires. Class-A seamless is effectively a 24 GB+ feature. Chosen defaults: 24–32 GB → 1 hot + 2 ring, mid ring at PROXY LOD, shared texture pool capped ~1.5 GB.
  

  
08
## Phasing
  

    

      
V1 — prove the seam
      
#### Contiguous-cluster seamless
      
        
- Offline world-layout audit tool; A/B/C seam classification
        
- De-singleton-ize ObjectSpace + AISpaceBase; per-cell CDB set
        
- Clamp-not-abort switch manager; (cell_id, element) collision results
        
- Residency ring + two-clock prefetch; atomic publish
        
- Class-A live crossing on 24 GB+; Class B/C = fast async
        
- xrSim always-on statistical layer; determinism firewall
      
    
    

      
V2 — GPU-driven residency
      
#### Bindless + ICB + sparse
      
        
- Per-cell MTLResidencySet + placement/sparse heaps
        
- Bindless tier-2 descriptor table; residency-change-only ICB
        
- GPU-feedback sparse texture streaming
        
- Cell-band FLOD / impostor LOD across the cluster
        
- Authored re-layout promoting Class B → Class A
      
    
    

      
V3 — scale & polish
      
#### Sub-tiling, RT, seam GI
      
        
- Sub-cell tiling of oversized levels (re-cut bakes)
        
- Hybrid RT shadows/GI to hide baked lighting seams
        
- Widened vehicle handoff zones
        
- Zone-wide impostor bake pipeline
      
    
  

  
### What to build first — ordered, non-negotiable
  
    
- Offline world-layout audit tool. Parse real CoC game.graph + all.spawn; dump offsets + AABBs; classify all seams A/B/C. Nothing else starts until this exists.
    
- Footprint + OPCODE-build microbenchmark on the 3–4 heaviest levels. Sizes rings and the prefetch horizon.
    
- level_id() consumer audit. Grep every single-level assumption in src/xrGame; this scopes the real refactor.
    
- GetStaticTris / result.element consumer audit. Scopes the collision fan-out correctly.
    
- Then de-singleton-ize, clamp the switch manager, build the ring.
  

  
09
## Top risks, each resolved

  

    
P0-1The design rests on unmeasured CoC offset data
    

      
The fields exist; the claim that offsets place cells abutting and non-overlapping is untested — no gamedata in-repo. Modpack offsets were authored to connect the graph, not to tile geometry.
      
Resolved: the audit tool (build #1) is a hard gate. Its A/B/C classification sets the real scope before a line of the residency manager is written. Do not let anyone reorder this.
    
  

  

    
P0-2"No loading anywhere" is oversold
    

      
The fog-band-replaces-load-screen framing is true only for Class-A seams. Teleport seams cannot be seamless; the destination isn't spatially where you'd walk.
      
Resolved: ship the three-tier pitch (§1) — hitchless within a cluster, sub-second async at boundaries, statistical everywhere. The fog band is a Class-A device only.
    
  

  

    
P0-3Residency cadence vs. seam-arrival guarantee
    

      
A 1 Hz recompute can't guarantee a 2–5 s async load finishes before a 6 m/s sprinter reaches a seam.
      
Resolved: two decoupled clocks (§4) — slow 1 Hz steady-state, plus a continuous seam-proximity prefetch that fires on cell-enter. Horizon sized from measured worst-cell load time.
    
  

  

    
P1-4CDB fan-out breaks every GetStaticTris caller
    

      
Not a perf question — a correctness one. Feel_Vision.cpp:32 indexes a single flat triangle array by result.element. Under multi-model collision that reads the wrong triangle or goes out-of-bounds (fatal on Darwin). Many call sites do this — material lookup, decals, footsteps.
      
Resolved: the fan-out result carries (cell_id, element), not a bare element. Grep every GetStaticTris / GetStaticModel / GetStaticVerts / .element consumer and migrate each to resolve through the owning cell. This is the true bulk of the collision refactor.
    
  

  

    
P1-5Single-level invariant is multiple asserts + silent script assumptions
    

      
The fatal VERIFY exists at :51, :136, and :195 — miss one and a seam aborts. Worse, downstream game/script code reads level_graph().level_id() assuming online == loaded level; wrong-cell nav results are a silent bug, harder than a crash.
      
Resolved: convert every level-id invariant to a logged clamp, and do the level_graph() / set_current_level / level_id() consumer audit now (build #3), classifying each as cell-relative-safe or single-level-assuming.
    
  

  

    
P1-6Hysteresis is path-dependent → replay divergence at budget draw
    

      
If materialize budget is drawn over the actual resident set, a fast vs. slow player has different resident cells at the same sim_time (hysteresis is path-dependent by design) → different budget-exhaustion point → different entities → replay breaks.
      
Resolved: materialize + budget draw over the ideal resident set (pure function of player region + Director focus at sim_time), never the I/O-completed set. Actual lags ideal; render/physics pop in late. Saves store ideal residency.
    
  

  

    
P2-7Baked lighting / HOM seams have no cheap runtime fix
    

      
Two independently-baked lightmaps meet at a Class-A seam; a fog band won't hide a daylight brightness step or a sun-cascade seam.
      
Accepted for V1: author seam locations at natural fog/tunnel/vegetation chokepoints where a band reads as intentional. True fix (RT shadows/GI) is deferred to V3. Don't pretend the band solves it.
    
  

  

    
P2-8HOM can't cull across a seam → over-draw where memory is worst
    

      
Two resident cells render with only frustum + coarse-hull culling at the boundary — a perf cliff exactly where you also hold two full cells' memory.
      
Mitigated / accepted: author seam portals narrow (doorway/tunnel) so the traverser clips the neighbor to a small frustum slice; where the seam is open terrain, force the neighbor to MID/coarse-hull band. Budget the two-full-cell case as the worst case, not the common one.
    
  

  

    
P2-9Two physics worlds at a live seam
    

      
Static-world raycasts fan out fine, but a dynamic ODE body lives in ONE physics world; a grenade rolling across the seam collides with cell A only until handoff.
      
V1 rule: a dynamic body is owned by exactly one cell's physics world (the cell containing its origin); static queries fan out, broadphase is single-cell. Accept minor clipping for objects exactly on the plane. Vehicles restricted to Class-A seams with a widened handoff zone, or deferred.
    
  

  xrStream · Seamless Whole-Zone Streaming — synthesis of 5 design fragments + skeptical review.

  Verified in-repo: global frame game_graph_space.h:35,77 · single Static CDB xr_area.h:39 · fatal invariants alife_switch_manager.cpp:51,136,195 · load-screen teardown alife_update_manager.cpp:198 · flat-index hazard Feel_Vision.cpp:32.

  Unmeasured & gating: CoC offset layout, per-cell footprint, OPCODE build time — resolve via the offline audit tool before implementation.

---

# Appendix — detailed design fragments (repo-grounded)

## X-Ray Level Structure & Why Loading Screens Exist — the per-level baked ground truth that blocks a seamless Zone, and the pragmatic streaming path within it.

X-Ray is a per-level-baked engine: everything spatial is compiled offline per level and swapped wholesale at a transition, which is exactly why loading screens exist. Six distinct baked artifacts load per level and must ALL be swapped together. (1) Render geometry: level.geom (VB/IB/SWIs) plus the fsL_VISUALS/fsL_SECTORS/fsL_PORTALS/fsL_LIGHT_DYNAMIC chunks in level (the main .cform-sibling file), loaded all-at-once in CRender::level_Load (src/Layers/xrRender_R2/r2_loader.cpp:17-109) with a matching level_Unload that frees every VB/IB/visual/sector. (2) Collision: level.cform is a single monolithic OPCODE/xrCDB AABB tree built into one CObjectSpace::Static model (src/xrCDB/xr_area.cpp:88-203, CObjectSpace::Load hard-codes "$level$","level.cform"); there is exactly one Static per world, so two levels cannot have collision resident simultaneously without merging trees. (3) Sectors+Portals: baked visibility cells, rebuilt into rmPortals CDB model in LoadSectors (r2_loader.cpp:309-446). (4) HOM occluder mesh: HOM.Load()/HOM.Unload() (r2_loader.cpp:98,120) — a separate baked occlusion mesh per level. (5) AI navigation: level.ai is a compressed node grid (NodeCompressed13, LevelStructure.hpp:443-545) loaded as ONE CLevelGraph in AISpaceBase::Load, which also calls game_graph().set_current_level(id) and binds the level's cross-table by GUID (src/xrAICore/AISpaceBase.cpp:17-34). This is the single hardest invariant: only ONE level graph is resident, and CALifeSwitchManager asserts an object may only be online when its game-vertex level_id() == the loaded level_graph().level_id() (src/xrGame/alife_switch_manager.cpp:195-198, 160-161). (6) Game-level connectivity: game.graph (CGameGraph, src/xrAICore/Navigation/game_graph.h) holds all levels' vertices plus cross-tables; it is the ONLY structure that already lives in a unified world space — SLevel carries an Fvector m_offset (game_graph_space.h:35) and each CGameVertex stores both tLocalPoint and tGlobalPoint (game_graph_space.h:76-77), i.e. the toolchain already computes a global position for every graph node. The transition mechanism itself is CSE_ALifeLevelChanger space-restrictors (src/xrGame/level_changer.cpp) firing M_CHANGE_LEVEL; CALifeUpdateManager::change_level autosaves and rewrites the actor's m_tGraphID/m_tNodeID/o_Position from the packet (src/xrGame/alife_update_manager.cpp:134-216), then the engine tears down and reloads all six artifacts. Crucially, ALife ALREADY simulates the whole Zone continuously: CALifeGraphRegistry keeps every object bucketed by game-vertex across ALL levels (m_objects[graph_id]), and setup_current_level() only promotes the CURRENT level's objects into a CALifeLevelRegistry and loads that one AI graph (src/xrGame/alife_graph_registry.cpp:70-100). Off-level objects run in OFFLINE mode with no geometry/nav resident. So the honest verdict: the Zone-wide sim and the global coordinate system already exist; the loading screen exists purely because RENDER geometry, COLLISION (single Static CDB), HOM, and the SINGLE-level AI graph are baked per level and swapped monolithically. Making it seamless does NOT require recompiling one giant map (that would blow the u16 sector/portal ids, the single-Static-CDB assumption, xrCDB's 24-26 bit node-link budget in NodeCompressed13, and lightmap atlas limits). The pragmatic answer is STREAM ABUTTING LEVEL-CELLS: keep each authored level as its own baked cell, but (a) make CObjectSpace hold a set of Static CDB models keyed by level-id instead of one, offset each into world space via SLevel::m_offset; (b) allow N resident CLevelGraphs and lift the single-level invariant in the switch manager so objects near a seam on the neighbor level can be online; (c) load render/HOM/sector data for the actor's cell plus its graph-adjacent neighbors, unload cells beyond a ring; (d) drive residency off game.graph edges (which already encode which levels abut) rather than distance alone. A hybrid touch: for tiny connective corridors, pre-merge them into their larger neighbor at bake time to reduce seam count. The full-recompile "one map" path is a trap on this engine and should be rejected.

**Mechanisms:**

- **Per-cell CDB collision set** (On cell enter/exit ring transitions): Replace CObjectSpace's single Static model (xr_area.cpp:88-203) with a map<level_id, CDB::MODEL> loaded from each cell's level.cform, each transformed by SLevel::m_offset into world space; raycasts/queries fan out to the cells overlapping the query AABB. Keep the objspace.bin CDB cache per cell (already keyed by $level$ path).
- **Multi-level AI graph residency** (When a neighbor cell is promoted to resident): Change AISpaceBase (AISpaceBase.cpp:17-44) from one m_level_graph to a set keyed by level-id; keep game.graph's cross-tables (already GUID-bound per level) to translate level-vertex<->global game-vertex across resident cells. Lift the single-level online invariant in alife_switch_manager.cpp:195-198 so seam-adjacent neighbor objects can go online.
- **Graph-adjacency streaming ring** (Each time actor crosses a cell boundary or a level-changer restrictor volume): Use game.graph edges (CGameGraph::begin/value, game_graph.h:70-72) to find levels adjacent to the actor's current level, not raw distance. Resident set = current cell + graph-neighbors within a hop budget. Prefetch render (r2_loader level_Load split per cell), HOM, sectors for the ring; unload beyond it (level_Unload already exists, r2_loader.cpp:111-197).
- **Seam handoff without change_level teardown** (Actor enters a seam volume between two resident cells): Convert CSE_ALifeLevelChanger (level_changer.cpp) from a full M_CHANGE_LEVEL autosave+reload (alife_update_manager.cpp:134-216) into a lightweight actor re-parent: since neighbor cell geometry/CDB/graph are already resident, just update actor m_tGraphID/m_tNodeID and continue — no save, no unload.
- **World-space offset bake pass** (Offline (xrAI/level compile time)): Author/generate per-level SLevel::m_offset (already in game_graph_space.h:35 and computed into tGlobalPoint) so abutting cells actually align edge-to-edge in world coordinates; add a bake-time validation that seam AI nodes and portals line up across cell boundaries.
- **Metal residency mapping** (Cell promote/demote): On unified memory M-series, map each resident cell's VB/IB and CDB to an MTLResidencySet per cell (per engine-v2 R2 plan); adding/removing a cell = add/remove its residency set, giving hitch-free streaming since no CPU->GPU copy is needed on Apple unified memory.

**Constraints / X-Ray realities:**

- level.cform is loaded into a SINGLE global CObjectSpace::Static CDB model with a hard-coded "$level$","level.cform" path (xr_area.cpp:93) — one collision tree per world today; must become a keyed set.
- Exactly ONE CLevelGraph is resident and the switch manager ASSERTS object.level_id()==loaded level_graph().level_id() for online status (alife_switch_manager.cpp:195-198) — the core single-level invariant.
- On Darwin XRAY_EXCEPTIONS=0: the dangling-id/level-mismatch R_ASSERT/VERIFY paths (AISpaceBase.cpp:26-30, alife_switch_manager.cpp:195) are FATAL aborts, not recoverable throws — seam handoff code must validate ids two-phase before mutating, never rely on catch.
- Sector/portal ids are u16 and per-level (LevelStructure.hpp fsL_SECTORS/PORTALS; LoadSectors r2_loader.cpp:309) — a single merged giant map would overflow id space and break the largest_sector heuristic.
- xrCDB node links are 24-26 bit (NodeCompressed13 NODE_BIT_COUNT=26, LevelStructure.hpp:474) — a full-world nav merge risks exceeding the link budget; keep graphs per-cell.
- CGameGraph::set_current_level tracks ONE current level (game_graph.h:77) and cross-table is swapped per level by GUID (AISpaceBase.cpp:22-27) — multi-residency needs cross-table lookups keyed by level, not a single current pointer.
- change_level today does a full autosave + total unload/reload (alife_update_manager.cpp:191-216) — this is the visible loading screen; seam handoff must bypass it entirely for adjacent resident cells.
- HOM occluder mesh and baked lightmaps are per-level; overlapping cells will have independent occlusion/lighting bakes that won't cull or light across seams without extra work.

**Open questions:** How many CoC cells realistically fit resident at once on 16-32GB unified memory? Each cell = VB/IB + one CDB Static + one CLevelGraph + HOM; need a memory budget per cell to size the streaming ring.; Do abutting CoC levels actually share world-space geometry at their seams, or do level-changers teleport across a non-contiguous gap (many vanilla transitions are teleports, not physical adjacency)? If teleports, 'seamless' means fast masked streaming, not literal edge-to-edge continuity.; Can the switch-manager single-level invariant be relaxed safely, or does too much game/script code assume 'online == on the one loaded level'? Needs an audit of level_graph().level_id() consumers.; Does xrCDB support querying a set of transformed CDB models efficiently (spatial partition over cells), or does GetNearest/raycast need a new broadphase over resident cells?; How do baked lightmaps and HOM behave at seams — is per-cell independent lighting acceptable visually, or is a runtime blend/GI needed (ties into engine-v2 R3 hybrid RT)?

## The Seamless Streaming Architecture — unified world space + proximity-driven cell streaming (xrStream)

Ground truth from the repo dictates the whole design. Two facts are load-bearing. (1) A global world coordinate frame ALREADY EXISTS in CoC data: `GameGraph::SLevel` carries `Fvector m_offset` and `CGameVertex` stores both `tLocalPoint` (level-local) and `tGlobalPoint` (world) (src/xrAICore/Navigation/game_graph_space.h:33-95). xrAI computed per-level offsets when it baked the game graph, so every CoC level already has an authored position in one continuous frame. We do NOT need to recompute or re-bake a single world — we adopt the game-graph offsets as the world origin and treat each level as a placed CELL. (2) Everything else is a hard singleton bound to ONE level: `CObjectSpace ObjectSpace` is a single member of `IGame_Level` with one `CDB::MODEL Static` collision DB (src/xrEngine/IGame_Level.h:80, src/xrCDB/xr_area.h:39); `IGame_Level::Load(dwNum)` loads one CFORM, sizes ONE `SpatialSpace` to that level's bounding volume, and calls `Render->level_Load` once (src/xrEngine/IGame_Level.cpp:103-146); `IGame_Level::Destroy` does a wholesale `Render->level_Unload` + `Objects.Unload` (IGame_Level.cpp:43-71); the render dsgraph holds flat `xr_vector<CSector*> Sectors / CPortal* Portals` for one level (src/Layers/xrRender/r__dsgraph_structure.h:65-66); and `CLevelChanger` fires `M_CHANGE_LEVEL` on touch, driving that teardown+reload behind a loading screen (src/xrGame/level_changer.cpp:44-51,110+). HONEST VERDICT: "one continuous world" is NOT a full world recompile and MUST NOT be. X-Ray's per-level baked artifacts (level.cform, level.ai nav graph, sectors+portals, HOM occluder mesh, baked hemi/sun lighting, detail-object slots) are authored per level and cannot be globally re-solved cheaply or correctly. So the design is: STREAM DISCRETE LEVEL-CELLS THAT ABUT, placed by game-graph offset, with the singletons refactored into a multi-resident CELL REGISTRY. A "cell" = one CoC level (the natural granularity — matches every baked artifact boundary; ~30+ cells for CoC). Optional future sub-cell tiling of huge levels is a later refinement, not V1; the level IS the cell because that is where all the baked data already partitions. The heart of the work is turning the level singletons into an array of independently loadable/unloadable ResidentCell records and adding a proximity streamer. A ResidentCell owns: its own `CDB::MODEL` collision island (transformed into world space by adding SLevel offset at load, OR kept level-local with a per-cell transform applied at query time — we keep level-local geometry + a cell world-transform to preserve baked precision and avoid float blowup far from origin), its render sub-dsgraph (its own Sectors/Portals, added to a global sector registry with cell-scoped ids), its level-graph nav sub-slice, its detail slots, and a generation counter. The global `SpatialSpace` (ISpatial_DB) is re-initialized ONCE to the union bounding volume of ALL cells (or a fixed large world box) at world init instead of per-level; static spatials from each cell insert/remove as cells promote/demote. Streaming is driven by hysteresis rings around the player and any Director-flagged focus: an INNER ring (full residency: render+CDB+nav+detail+textures) at e.g. one-cell-adjacency, a MID ring (proxy: coarse render impostor + coarse collision box + full nav sub-graph so OFFLINE agents still path) one ring out, and OUTER (unloaded, xrSim STATISTICAL only). Rings use asymmetric thresholds (load at ring boundary R, unload at R+margin) so a player pacing a seam never thrashes — this mirrors X-Ray's own online/offline `switch_distance*(1±factor)` idiom, reused here as a residency threshold. Because CoC levels are large and few, the "ring" is really cell-adjacency over the game-graph cross-level edges plus a euclidean fallback: a cell is a LOAD candidate when the player's cell or any cell reachable within N cross-level edges (N=1 for full, 2 for proxy) — this exploits the fact that the game graph already encodes which levels abut. All I/O and decompression run on worker threads; the main thread only ever does the atomic publish. The streamer is a state machine per cell: NOT_RESIDENT -> LOAD_QUEUED -> LOADING (worker: FS read of level.cform/geom/ai/sectors + texture pages, decompress, build CDB via `CDB::MODEL::build`, build sub-dsgraph, no globals touched) -> READY_TO_PUBLISH -> RESIDENT; and RESIDENT -> EVICT_QUEUED -> UNLOADING -> NOT_RESIDENT. Promotion is ATOMIC: the worker builds the entire ResidentCell off to the side (its own CDB, sectors, nav slice, GPU resources uploaded via Metal `MTLStorageModeShared` so no staging copy and the buffer is GPU-visible the instant it is written — unified memory is the enabler), then a single main-thread step swaps the cell pointer into the registry under a generation bump and inserts its spatials/sectors. No half-loaded cell is ever visible because the registry only ever holds fully-built cells; a cell mid-build is invisible to render/collision/query traversal. A frame budget caps publishes per frame (1 cell publish max, spread inserts) so even the atomic swap can't spike. The SEAM problem is handled per-domain: RENDER — CoC levels are authored to visually abut at level-changer portals; we render adjacent resident cells together and rely on distance/HOM culling; the seam gap (levels rarely tile perfectly) is masked with a thin skirt/fog band at the authored transition volume (the old load-trigger becomes a cosmetic transition, not a stop). COLLISION — each cell keeps its own CDB island; ray/box/nearest queries fan out to all resident cells' CDBs whose world AABB the query touches (cheap: 1-3 cells), so an actor straddling a seam collides against both islands; no global rebuild. NAV — the level-graph is already per-level with cross-tables (game_level_cross_table) linking level nav to game-graph vertices; we load nav sub-graphs for INNER+MID cells and stitch at the pre-authored cross-level edges, so OFFLINE agents path across the seam through the existing cross-table edges rather than teleporting. xrSim integration is the safety spine: cell residency is DERIVED state, never authoritative. xrSim owns all entity truth in world/region space (per the xrSim spec's stable u32 ids + region partition); streaming only decides how much fidelity to render/simulate locally. Entity handoff across a seam is a no-op for correctness because entities live in xrSim's global store addressed by stable id and region, NOT owned by a cell — so no dup/lost entity and no invalid-id at a boundary, which is exactly the failure the xrSim single-authority rule was built to prevent. When a cell promotes to RESIDENT, its OFFLINE/STATISTICAL entities MATERIALIZE via xrSim's existing serialized region-ordered materialize pass (attaching render+physics proxies to existing ids); on demote they DISSOLVE back — streaming reuses xrSim's promotion machinery rather than inventing a parallel one. DETERMINISM: the streamer must not perturb xrSim replay. We enforce this by making residency purely a function of xrSim's deterministic state (player region + Director focus flags), never of wall-clock I/O timing, and by NEVER letting a stream event mutate sim state — materialize/dissolve are already deterministic sim ticks. If a cell hasn't finished loading when the player crosses a seam (I/O too slow), the sim does NOT stall: OFFLINE agents keep pathing on the nav sub-graph (loaded first, cheapest), physics/render just pop in a few ticks late — a fidelity delay, never a determinism break, because sim truth was already present. Save/replay stores xrSim state + player region, and residency is reconstructed on load, so a replay produces identical sim regardless of how streaming happened to schedule.

**Mechanisms:**

- **Cell = one CoC level, placed by game-graph offset** (World init reads all SLevel offsets once; cells created lazily on first residency.): Adopt existing GameGraph::SLevel::m_offset + CGameVertex::tGlobalPoint (game_graph_space.h:34,79) as the unified world frame. Each level becomes a ResidentCell with a world transform = its SLevel offset. No re-bake. Geometry stays level-local (preserves baked float precision, avoids far-from-origin blowup); the cell transform is applied at query/draw time. Sub-cell tiling of oversized levels is deferred.
- **Multi-resident cell registry (de-singleton-ize the level)** (On cell promote/demote.): Refactor the IGame_Level singletons into an array of ResidentCell. Replace the single CObjectSpace.Static CDB (xr_area.h:39, IGame_Level.h:80) with one CDB::MODEL per cell; SpatialSpace (ISpatial_DB) initialized ONCE to the union world box instead of per-level (IGame_Level.cpp:128); render dsgraph Sectors/Portals (r__dsgraph_structure.h:65) become a global registry keyed by cell-scoped ids. level_Load/level_Unload (IGame_Level.cpp:43,139) become per-cell, not whole-game.
- **Hysteresis residency rings (INNER full / MID proxy / OUTER statistical)** (Coarse 1-4 Hz residency pass over cell set, driven by player region from xrSim (deterministic input, not wall clock).): Compute a cell's target residency from cross-level-edge distance to the player's cell (N<=1 => full, N<=2 => proxy, else statistical) plus a euclidean fallback. Load at ring boundary R, unload only past R+margin (asymmetric) to kill boundary thrash — reuses X-Ray's own switch_distance*(1±factor) hysteresis idiom as a residency threshold. Director-flagged focus cells (from xrSim) add extra ring anchors.
- **Worker-thread load + async I/O/decompress pipeline** (Background, continuous; bounded worker pool; per-cell priority = ring proximity.): On LOAD_QUEUED, a TaskManager worker reads level.cform/geom/ai/sectors + texture pages from FS, decompresses, builds the CDB island via CDB::MODEL::build (xrCDB.h:114), builds the sub-dsgraph, and uploads GPU resources into MTLStorageModeShared buffers (unified memory => GPU-visible with zero staging copy). Touches NO global state — the cell is built entirely off to the side. Nav sub-graph loads FIRST (cheapest, keeps OFFLINE agents pathing across a not-yet-rendered seam).
- **Atomic publish (no half-loaded state ever visible)** (Main thread, start-of-frame, budgeted.): Worker signals READY_TO_PUBLISH. A single main-thread step swaps the finished ResidentCell pointer into the registry under a generation bump, then inserts its spatials into SpatialSpace and its sectors into the sector registry. Because the registry only ever references fully-built cells, render/collision/query traversal can never see a partial cell. A frame budget allows <=1 publish/frame with insert-spreading so the swap itself never hitches.
- **Per-cell CDB collision islands with query fan-out (seam collision)** (Per collision query.): Keep each cell's CDB separate; ray/box/nearest queries fan out only to resident cells whose world AABB the query overlaps (typically 1-3). An actor straddling a seam collides against both islands with no global CDB rebuild. Cell world-transform applied to query inputs.
- **Nav sub-graph stitching via existing cross-tables (seam nav)** (On cell promote to >= MID (proxy) ring.): Load level-graph nav slices for INNER+MID cells; connect them at the pre-authored cross-level edges already in game_level_cross_table / CLevelChanger next-graph-id (level_changer.cpp:49-50). OFFLINE/ONLINE agents path across seams through these existing edges — no invented stitching, no teleport.
- **xrSim-owned entity handoff (no dup/lost/invalid-id at seams)** (On residency change; materialize trickled over N ticks to avoid pop-in armies.): Entities are NEVER owned by a cell — xrSim's global SoA store owns them by stable u32 id + region (xrSim spec). Crossing a seam changes nothing about ownership. On cell promote, xrSim's serialized region-ordered materialize attaches render/physics proxies to existing ids; on demote, dissolve folds them back. Streaming reuses xrSim's promotion machinery; conservation invariant asserted per handoff (logged, not THROW — Darwin XRAY_EXCEPTIONS=0 safe).
- **Determinism firewall (streaming can't break replay)** (Invariant enforced at every residency decision and every save/load.): Residency is a pure function of deterministic sim state (player region + Director focus), never of I/O completion timing. Stream events NEVER mutate sim state directly. If I/O lags a seam crossing, OFFLINE agents keep simulating on the nav slice and physics/render pop in a few ticks late — a fidelity delay, not a sim divergence. Save stores sim state + player region; residency is reconstructed on load. Same accepted intents + snapshot => bit-identical replay regardless of stream scheduling.
- **Cosmetic transition band replacing the load screen** (Passive; when both abutting cells are RESIDENT.): The old CLevelChanger touch volume (level_changer.cpp) stops triggering M_CHANGE_LEVEL teardown; instead adjacent resident cells render together and the authored transition volume becomes a thin fog/skirt band masking any geometric gap where two levels don't perfectly tile. Player walks through; no stop.

**Constraints / X-Ray realities:**

- X-Ray bakes per-level: level.cform (collision), level.ai (nav graph), sectors+portals, HOM occluder mesh, baked hemi/sun lighting, detail slots. These CANNOT be globally re-solved cheaply or correctly. 'One continuous world' must be streamed abutting level-cells, NOT a full world recompile. This is the honest core constraint.
- Baked lighting/occlusion (HOM, hemi) is per-level and won't be globally consistent across a seam — expect lighting/occlusion discontinuities at seams; the transition fog band mitigates but does not fix. True fix (global GI) is out of scope for streaming and belongs to the Metal renderer track.
- CoC levels rarely tile perfectly in world space — the game-graph offsets place them approximately; visible geometric gaps/overlaps at seams are likely, requiring the cosmetic skirt/fog band. Some level pairs may need authored seam patches.
- The IGame_Level level-singletons (single CObjectSpace.Static CDB, single per-level SpatialSpace sized to one level, whole-game level_Load/level_Unload, flat Sectors/Portals) are pervasive — de-singleton-izing them is the bulk of the work and touches xrEngine, xrCDB, and both render layers. High-risk refactor.
- XRAY_EXCEPTIONS=0 on Darwin: seam conservation checks and dangling-ref handling must degrade to logged values, never THROW (which becomes a fatal VERIFY). All materialize/dissolve/publish error paths must be value-returning.
- OpenGL 4.1 ceiling on macOS caps concurrent resident geometry/texture budget; the Metal renderer (MTLStorageModeShared zero-copy publish, MTLResidencySet for cell resource sets) is what makes multi-cell residency cheap. On GL fallback, keep residency ring counts conservative.
- Streaming must not perturb xrSim determinism/replay — residency derived only from deterministic sim state, never from wall-clock I/O timing; this is a strict invariant, not a best-effort.
- Texture/VRAM budget: multiple full-detail cells resident simultaneously multiplies texture memory. Needs a texture-streaming layer (mip residency per cell) or aggressive MID-ring proxy detail reduction to stay in budget on 8-16GB unified-memory M-series.

**Open questions:** Do CoC's baked game-graph SLevel offsets actually place levels in non-overlapping, roughly-abutting world positions, or are many levels stacked at origin/arbitrary offsets (common in mod compilations)? If stacked, we need an authored world-layout pass to reposition cells — this changes 'no recompile' to 'lightweight re-layout'. MUST verify against real CoC all.spawn/game.graph data before committing.; How large is one CoC level's full resident footprint (CDB + textures + sectors + nav) on unified memory? Determines how many cells can be FULL-resident at once, i.e. whether the INNER ring can be >1 cell. Needs measurement.; Is per-query CDB fan-out across cells fast enough for hot paths (bullet traces, AI vision Feel_Vision uses GetStaticTris directly — src/xrEngine/Feel_Vision.cpp:32)? Those call sites assume a single Static model and must be rewritten to iterate cells; perf of the rewrite is unproven.; Baked lighting/HOM discontinuity at seams: is a fog band cosmetically acceptable, or do high-traffic seams need authored lighting-blend patches? Playtest-dependent.; Sub-cell tiling: are any individual CoC levels large enough that whole-level granularity blows the residency budget, forcing sub-level tiles in V1 rather than deferred? Depends on the footprint measurement above.; Texture streaming: build a per-cell mip-residency streamer now, or lean on MID-ring proxy detail reduction for V1? Affects VRAM headroom on 8GB M-series.; How does the ONLINE agent budget (xrSim global 150-200 cap) interact with multiple FULL-resident cells — does the streamer inform xrSim which cells are hot so admission prioritizes correctly across a multi-cell hot bubble?

## Coupling xrSim (whole-Zone band simulation) to seamless streaming + continuous cross-region interactions

The load-bearing insight is already true in this repo: X-Ray's game graph is ALREADY one continuous cross-level world. GameGraph::CVertex packs tLevelID:8 + tNodeID:24 (src/xrAICore/Navigation/game_graph_space.h:78-79) and its CEdge references any other _GRAPH_ID (u16, up to 65535 vertices) including vertices on OTHER levels via imported cross-tables (game_level_cross_table.h). Offline entities store position as m_tGraphID (game vertex, level-tagged) + m_tNodeID (level vertex) (src/xrServerEntities/xrServer_Objects_ALife.h:143,147). So the whole-Zone OFFLINE/STATISTICAL sim is intrinsically level-agnostic: it walks one graph spanning every CoC level. This is exactly why xrSim's band model (spec 2.4) can be whole-Zone from day one.

"Load the whole map without loading" therefore does NOT mean recompiling 30+ level.geom/cform/ai into one mega-level. Be honest: X-Ray bakes geometry, CFORM collision, AI-map, sectors/portals, and lighting PER LEVEL; a full recompile blows the u24 node id, the u16 graph id, HOM/sector budgets, and the per-level baked-light assumption. It means: xrSim runs the whole Zone continuously in bands, and STREAMING is an orthogonal decision about which level-cells have RESIDENT GEOMETRY. The band an entity may occupy is GATED by geometry residency; that gate is the entire coupling contract.

Define a strict band<->residency lattice. DORMANT and STATISTICAL require nothing loaded (pure graph/aggregate math, authoritative per spec 2.4). OFFLINE requires the game-graph + AI-map for that region resident but NOT render geometry or CFORM: it moves along graph vertices at 2 Hz, needs no physics or draw. ONLINE requires FULL residency: CFORM (collision), level.ai (fine nav), sectors/portals, and Metal BLAS, because ONLINE = physics + animation + reflex trees at 30 Hz which cannot run in an unloaded cell. This yields the hard invariant that fixes the classic switch bug: an entity's admissible band is min(desired_band, residency_ceiling(entity.pos_region)). The current code enforces the wrong version of this as a FATAL assert: add_online VERIFYs that a switching object's graph level equals the currently-loaded level (src/xrGame/alife_switch_manager.cpp:51) and on Darwin XRAY_EXCEPTIONS=0 that VERIFY is an abort. We replace assert-and-die with clamp-and-continue: if geometry is not resident, the entity is CAPPED at OFFLINE, never promoted, no THROW, exactly matching spec two-phase validation (2.5, C6).

Streaming topology: a residency manager tracks a set of resident CELLS (a cell = one imported CoC level, the natural bake unit). Player region + K-hop neighborhood on the region adjacency graph (spec 2.1) defines the ONLINE-eligible ring and the geometry prefetch ring. Because CoC levels abut at authored transition points (level.geom edges of adjacent maps meet at connectors), seamlessness = keeping the player's cell AND all cells reachable within the prefetch horizon resident simultaneously, and rendering across the cell boundary by treating each resident cell as its own sector graph, portaled together at the seam. Two abutting resident cells each keep their own CFORM/sectors/BLAS; the seam is a stitched portal, not a merged mesh. This is honest streaming of discrete level-cells that abut, not a world recompile, and it is the only approach compatible with the baked pipeline.

Handoff across a seam must obey the spec's conservation + stable-id invariants (2.5, C5). NO id regen ever: OFFLINE/ONLINE promotion is attach/detach of a proxy, never spawn/despawn (spec 2.5). When an entity's OFFLINE graph walk crosses a cross-level edge, we ONLY reassign m_tGraphID (as alife_simulator_base.cpp:125 already does) and, if the destination cell is resident and within the ONLINE ring, re-attach a physics/render proxy on the destination cell's CFORM at the corresponding level vertex. If the destination cell is NOT resident, the entity keeps walking as OFFLINE with no proxy: it is the SAME entity, same id, just proxy-less. This kills the A-Life dup/loss bug at its root because there is exactly one authoritative record in the SoA store regardless of which cells are loaded.

The player-standing-on-a-seam case is the hardest and the design's proof point. Both cells are resident (prefetch horizon guarantees it whenever the player is within transition range). An entity physically crossing the seam LIVE stays ONLINE the whole time: its proxy is detached from cell A's physics world and attached to cell B's at the shared portal vertex within a single sim tick, transform carried over, id unchanged, band unchanged. Because sim is a fixed 100ms tick decoupled from render via the accumulator (spec 2.3), the cross-attach happens atomically inside step_sim between two rendered frames, so there is no half-in-half-out frame. Conservation is asserted every such transition as a LOGGED value on XRAY_EXCEPTIONS=0, not a throw (spec 2.5): sum(live+dormant+statistical) per faction/species conserved modulo logged births/deaths.

Cross-region INTERACTIONS run continuously in the STATISTICAL/OFFLINE bands independent of residency, which is the whole point of "the whole Zone is always simulated" (spec 1.1/2.4). A faction squad marching A->B->C is a Squad FSM in TRAVELING doing HPA* over the region graph (spec 4.2); it advances every OFFLINE tick whether or not B is loaded, only materializing to ONLINE NPCs when it enters the player's resident ring. Wildlife migrating across a seam is a cohort fraction diffusing along a region-adjacency edge per ECO_TICK (spec 3.4 MigrationPressure); the seam is just a graph edge, so migration across an unloaded boundary is ordinary aggregate math. An emission sweeping multiple regions is the tick-scheduled staggered event (spec 3.5): per-region aftermath fires on the deterministic schedule regardless of residency, and only resident regions render the VFX/materialize casualties. Economy/goods flow along controlled trade routes on the region graph (spec 4.5) with no residency dependence. Events propagate under the information-latency model (spec 4.6): a WorldEvent's per-faction known flag spreads along squad line-of-contact, so a region flip in an unloaded cell becomes known to neighbors over game-hours exactly as if loaded. Residency changes NOTHING about interaction fidelity; it changes only whether you can SEE and physically touch it.

Determinism + replay: streaming order must never perturb sim. Materialization of a newly-resident region is a serialized, region-id-ordered, single-threaded pass drawing the global ONLINE budget in fixed order (spec 2.5 M1), excluded from the parallel-jobs phase, seeded from (region_id, sim_time). Therefore whether the player drives fast or slow, and whatever order cells page in, the drawn entities and the budget-exhaustion point are identical: streaming is a pure function of sim_time, not of I/O timing. All persisted authoritative position is fixed-point graph/level vertex ids (spec 6.2); floats live only in the transient ONLINE proxy and are re-derived on attach, so a save taken mid-seam-crossing restores identically. Record/replay (spec 6/roadmap A4) captures the residency ring as a derived signal, not an input, so replays reproduce band transitions exactly.

Metal/unified-memory payoff (roadmap V1/R2): residency maps 1:1 onto MTLResidencySet + placement/sparse heaps. Each cell's CFORM-adjacent GPU assets (vertex/index buffers, textures, static BLAS built at cell load per roadmap R3 line) live in a per-cell placement heap; making a cell resident = adding its heap to the residency set and its BLAS to the TLAS; evicting = the reverse. Because M-series unified memory means sim SoA buffers are directly GPU-readable without copy (spec 1.5), the OFFLINE->ONLINE materialize does not copy entity transforms to the GPU; it just flips the proxy's residency bit. Trickle-materialization (spec 2.5 M3, bounded entities/tick) doubles as a hitch guard: bounded per-tick BLAS refits and heap residency adds keep the frame flat, so seamless = no hitch, not merely no load screen.

Bottom line opinion: do NOT attempt one recompiled world. Ship "one continuous world" as (a) xrSim always-on over the single existing cross-level game graph in bands, plus (b) a residency ring that keeps the player's cell and its transition-reachable neighbors resident and portals them at seams, plus (c) the band<->residency lattice with clamp-not-abort promotion. That is honest to X-Ray's baked pipeline, fixes the historical switch bugs by construction, and is the natural shape for M-series streaming.

**Mechanisms:**

- **Band<->Residency lattice gate** (Recomputed on the coarse 1 Hz band-membership pass (spec 2.4) and on any cell residency change.): admissible_band = min(desired_band, residency_ceiling(pos_region)). DORMANT/STATISTICAL need nothing loaded; OFFLINE needs game-graph+AI-map; ONLINE needs full CFORM+level.ai+sectors+BLAS. Replaces the fatal VERIFY at alife_switch_manager.cpp:51 with clamp-not-abort (Darwin XRAY_EXCEPTIONS=0 safe).
- **Seam handoff (id-stable, no dup/loss)** (Per OFFLINE tick for graph-walk crossings; per sim tick (100ms) for live ONLINE crossings, between rendered frames via the accumulator.): OFFLINE cross: reassign only m_tGraphID across the cross-level edge (as alife_simulator_base.cpp:125), keep single SoA record, id unchanged. ONLINE live cross (player on seam): detach proxy from cell A physics world, attach to cell B's CFORM at shared portal vertex, carry transform, atomic within one step_sim tick.
- **Deterministic residency prefetch** (Driven by player region membership; horizon sized so a seam-crossing player always has both cells resident before arrival.): Keep resident: player region + K-hop neighbors on region graph + all transition-reachable cells within prefetch horizon. Cells page via placement/sparse heaps added to MTLResidencySet; static BLAS built at cell load, added to TLAS.
- **Order-independent materialize** (On region entering the ONLINE ring; excluded from the parallel-jobs phase (spec 2.5 M1).): Newly-resident region draws concrete entities in a serialized region-id-ordered single-threaded pass, global ONLINE budget consumed in fixed order, PRNG seeded (region_id, sim_time), trickled over N ticks. Streaming becomes a pure function of sim_time, not I/O timing.
- **Residency-independent cross-region interaction** (OFFLINE 2 Hz round-robin (id%5), ECO_TICK 300 ticks, emission on deterministic schedule, event latency along squad line-of-contact.): Squad TRAVELING HPA* over region graph, cohort migration diffusion per ECO_TICK, emission staggered per-region schedule, trade-route goods flow, WorldEvent known-flag propagation — all run in STATISTICAL/OFFLINE regardless of which cells are loaded. Residency only decides render/physics, never interaction fidelity.
- **Conservation + hitch guard** (Every band transition, every seam crossing, every materialize step.): Assert sum(live+dormant+statistical) per faction/species at every transition and seam crossing as a logged value (no THROW). Trickle-materialize + bounded per-tick BLAS refit/heap adds keep the frame flat.

**Constraints / X-Ray realities:**

- X-Ray bakes geometry, CFORM, AI-map, sectors/portals, and lighting PER LEVEL. A single recompiled mega-level is a non-starter: it overflows the u24 tNodeID and u16 _GRAPH_ID id spaces, HOM/sector budgets, and breaks per-level baked lighting. 'One continuous world' MUST be streamed abutting level-cells, not a world recompile.
- The current promotion path is fatal-by-design on Darwin: add_online VERIFYs graph level == loaded level (src/xrGame/alife_switch_manager.cpp:51); with XRAY_EXCEPTIONS=0 THROW/VERIFY aborts (CLAUDE.md). The lattice gate MUST clamp band instead of asserting, or every off-cell promotion crashes.
- OpenGL 4.1 caps macOS today; the residency/heap/BLAS mechanisms are only fully available on the Metal renderer (roadmap V1/R2/R3). On the GL path, streaming must fall back to coarse per-cell load with the ONLINE ring restricted to the single player cell (no live seam crossing across two GL-resident cells).
- Keeping multiple full cells resident (CFORM + sectors + BLAS + textures) multiplies VRAM/unified-memory pressure vs classic single-level load. Prefetch horizon must be tuned against the M-series unified memory budget; sparse/placement heaps and aggressive eviction of non-adjacent cells are mandatory, not optional.
- Baked lighting differs per cell; at a live seam two independently-baked lightmaps meet and can seam visibly. Mitigation (light-probe blend / SSGI) is a renderer problem outside this dimension but constrains how convincing 'seamless' looks.
- Two-phase validation (spec 5.3) must re-check residency AND liveness at apply-tick: a Director intent targeting an entity that dissolved or an id that became proxy-less between ingest and apply must degrade to aggregate or drop-with-feedback, never dangle into a THROW.
- Determinism forbids letting I/O completion order affect sim: materialize order, budget draw order, and PRNG seeding must depend only on (region_id, sim_time), never on which cell's heap finished paging first.

**Open questions:** Cell granularity: is 'cell = one imported CoC level' the right bake unit, or should large levels be sub-tiled for finer streaming/eviction? Sub-tiling means re-cutting bakes (CFORM/sectors), which reintroduces a partial recompile cost.; Prefetch horizon depth (K hops) and per-cell unified-memory budget on baseline M-series (e.g. M1/M2 8-16GB vs M-Max): how many full cells can be co-resident before eviction thrash at a seam?; Live seam physics: two abutting cells have independent CFORM/collision worlds. Do we run two ODE/physics worlds and hand off, or stitch a shared collision region at the seam? Hand-off is simpler but a body spanning the exact seam plane is a corner case needing a rule.; Baked-light seam blending strategy at a live two-cell boundary (probe blend vs screen-space) — needed for 'seamless' to look seamless, not just be hitch-free.; Cross-level portal authoring: CoC transition points are teleport-style, not physically abutting geometry. Do we need to author physical seam geometry between originally-disconnected maps, or accept that only maps that spatially abut can be truly walked across while others remain fast-travel?; Does the ONLINE global budget (150-200, spec 2.3) need per-cell sub-quotas when two cells are co-resident at a seam, to prevent one cell starving the other's visible NPCs?

## Renderer & Asset Residency for a streamed continuous Zone on the native Metal renderer (R1/R2)

Be honest first: X-Ray bakes everything per-level. `CRender::level_Load` (src/Layers/xrRender_R2/r2_loader.cpp:17) loads one monolithic VB/IB set (LoadBuffers, lines 199-278), one flat `Visuals` array (LoadVisuals, 280), one sectors/portals graph (LoadSectors, 309), one HOM occluder mesh (HOM.Load, 98), and one baked light set (301) — all keyed to `$level$`. Sectors and portals are per-level structures (src/Layers/xrRender/r__sector.h): a `CPortal` connects exactly two `CSector`s that only exist inside one level's file; HOM is a single CDB occluder model (src/Layers/xrRender/HOM.h). There is no cross-level portal, no whole-Zone occluder, no shared geometry pool. So "one continuous world" honestly means: stream discrete, pre-baked level-cells that abut at their old transition points, keeping each cell's own sectors/portals/HOM/lighting intact, and stitch them with new cross-cell seam structures — NOT a full world recompile (that would require re-baking lighting, CDB, sectors and AI-graph across 30+ CoC levels, which is a content-pipeline project, not a runtime one, and out of scope for V1). The design is therefore a CELL STREAMING system, where a "cell" is an imported CoC level, and the renderer treats resident cells as a variable set each frame. This maps cleanly onto Metal residency primitives and unified memory. Residency model: one persistent bindless world descriptor table (argument buffer tier 2) indexing every possibly-visible resource; per-cell `MTLResidencySet`s that are added/removed from the command-buffer/queue residency as cells promote/evict, so the driver keeps exactly the working set wired and pages the rest — this replaces X-Ray's all-or-nothing level_Unload (r2_loader.cpp:111). Geometry and textures live in per-cell sub-allocations of large `MTLHeap`s (placement heaps for geometry, sparse heaps + sparse `MTLTexture`s for the huge world texture set) so a cell's memory is a heap region that can be released as one unit without fragmenting the global pool. Unified memory (`MTLStorageModeShared`) means the streaming thread `memcpy`s decoded geometry/texture bytes straight into the heap-backed buffer/texture the GPU reads — no staging blit, no `didModifyRange`, killing the LoadBuffers Map/Unmap-upload round-trip (r2_loader.cpp:242-244) that currently stalls level load. Occlusion across the seam: keep each cell's baked HOM + portals as the intra-cell fine occluder (it is correct and cheap), and add a coarse whole-Zone occluder — a low-poly hull per cell plus explicit seam portals connecting a cell's boundary sector to the abutting cell's boundary sector — so portal traversal (CPortalTraverser::traverse, r__sector.h:127) can walk from the player's cell into neighbors through seam portals, and a Zone-level software/compute rasterized depth (the modern replacement for HOM's CDB rasterizer, src/Layers/xrRender/occRasterizer.h) culls whole distant cells before their fine graph is ever touched. Draw distance / seeing across the Zone: near cells render full detail through the normal dsgraph; mid cells render only their coarse hull + FLOD impostors (FLOD already exists, src/Layers/xrRender/FLOD.h — an 8-facet billboard cross, the exact primitive for this); far cells collapse to a single baked impostor/heightfield card per cell. This is a geometry-LOD-by-cell band system that mirrors the sim's ONLINE/OFFLINE/STATISTICAL bands, and should be driven by the same region/proximity signal so render residency and sim fidelity stay coherent. Render graph for a variable resident set (R2): the frame is GPU-driven — one indirect command buffer (ICB) encoded from the resident cells' draw args, culled on the GPU against the coarse occluder + frustum, indexing the bindless argument-buffer table; the CPU never re-encodes per-cell draw lists, it only edits the residency set and the descriptor table when a cell promotes/evicts. Because the resident set changes at cell boundaries (not per-frame), the ICB is rebuilt only on residency change (double-buffered, atomic pointer swap at frame boundary) and reused verbatim on steady-state frames — zero per-frame allocation, satisfying R2's exit criterion. Streaming is off-thread with a bounded budget: decode + heap-fill happens on an I/O/decode thread; the render thread only observes "cell N is resident" via a completion flag and flips it into the descriptor table + residency set at a frame boundary, so a half-loaded cell is never referenced (the correctness rule that prevents dangling bindless indices — critical on XRAY_EXCEPTIONS=0 Darwin where a bad ref would VERIFY-abort, not throw). Textures: the world texture set is far larger than VRAM-equivalent working set, so use sparse `MTLTexture`s with per-tile residency driven by a GPU feedback pass (sample-count / access-pattern buffer) so only mip tiles actually sampled this frame are resident — the standard Apple sparse-texture streaming loop — and back all cell textures with a shared sparse heap so eviction is per-tile, not per-texture. Keep a small always-resident low-mip tail per texture so a suddenly-visible surface shows a blurry-but-correct frame instead of a hole (no pop-to-nothing hitch).

**Mechanisms:**

- **Per-cell MTLResidencySet lifecycle** (On cell band transition (near/mid/far/unloaded), gated by proximity + sim region signal, at a 1 Hz-ish residency recompute, never per-frame.): Each streamed cell owns one MTLResidencySet holding its geometry heap allocation, texture heap allocations, and light/const buffers. Promote = residencySet.addAllocation + commandQueue.addResidencySet; evict = removeResidencySet + heap region free. Replaces the monolithic level_Unload teardown (r2_loader.cpp:111-197) with per-cell add/remove.
- **Bindless world descriptor table (argument buffer tier 2)** (Table edited only on residency change; read every frame by the ICB.): One persistent argument buffer indexes every resource of every possibly-visible cell (textures via sparse handles, geometry via heap-relative buffer+offset). Cell promote writes its slot range; evict clears it. GPU draws index by cell/material id — no per-cell CPU binding.
- **Placement + sparse MTLHeaps for cell pools** (Alloc on cell load, free on cell evict; tile map/unmap on sparse feedback.): Large placement heap for geometry (each cell = one contiguous sub-region, freed as a unit — no fragmentation), sparse heap for textures with per-tile mapping. Unified-memory Shared storage means decode thread memcpys bytes directly into the heap resource the GPU reads; no staging copy or blit.
- **GPU-feedback sparse texture streaming** (Feedback read + tile residency update every few frames (amortized), not every frame.): A lightweight pass writes which texture tiles/mips were sampled into a feedback buffer; the streaming thread maps in needed tiles and unmaps cold ones on the shared sparse heap. Always keep a low-mip tail resident so newly visible surfaces are blurry-correct, never a hole.
- **Cell-band geometry LOD (full / coarse-hull+FLOD / single impostor)** (Band chosen per cell from proximity + sim region LOD; recomputed on the residency pass.): Near cells render the full dsgraph; mid cells render a coarse hull mesh + FLOD impostors (FLOD.h already provides the 8-facet billboard); far cells collapse to one baked impostor card per cell. Lets the player SEE across the whole Zone at bounded cost.
- **Seam portals + coarse whole-Zone occluder** (Seam portals authored at import from old transition points; coarse occluder rebuilt on residency change, tested per frame on GPU.): Keep each cell's baked HOM + portals for intra-cell occlusion (correct, cheap). Add explicit seam portals linking abutting cells' boundary sectors so CPortalTraverser walks across cells, plus a GPU-rasterized coarse per-cell-hull depth (modernized occRasterizer) to reject whole distant cells before touching their fine graph.
- **Residency-change-only ICB rebuild** (Rebuild on cell promote/evict; reuse every other frame — zero per-frame allocation.): One GPU-driven indirect command buffer encodes all resident cells' draws, culled on-GPU against coarse occluder+frustum, indexing the bindless table. Rebuilt (double-buffered, swapped at frame boundary) only when the resident cell set changes; reused verbatim on steady-state frames.
- **Frame-boundary atomic cell publish** (On decode completion, applied at next frame boundary.): Decode/heap-fill runs on an I/O thread; a cell becomes referenceable only when its residency set, descriptor slots, and ICB entry are all complete, published via one atomic flip at a frame boundary. A half-loaded cell is never indexed — prevents dangling bindless refs that would VERIFY-abort on XRAY_EXCEPTIONS=0.

**Constraints / X-Ray realities:**

- X-Ray bakes per-level: geometry, sectors/portals, HOM occluder, and lighting are all keyed to $level$ (r2_loader.cpp:62-101). 'One continuous world' at runtime = streaming abutting pre-baked cells + new seam structures, NOT a whole-world recompile (which needs re-baking CDB/lighting/sectors/AI-graph across 30+ levels — a content-pipeline effort, out of V1 scope).
- Cross-cell lighting/shadow seams: each cell's lights are baked to its own space; sun cascades and light lists differ across a seam. Seam regions will show lighting discontinuities unless a runtime relight or cross-cell light-list merge is added — honest visual limitation of cell streaming.
- Portals only ever connected two same-level sectors (r__sector.h:44). Seam portals are a NEW structure with no baked data; must be authored at import from old transition points, and their two sides live in different heaps/residency sets.
- Metal 3 residency: MTLResidencySet requires recent OS; sparse textures + argument buffers tier 2 are broadly available on M-series but tile-residency APIs and ICB details drift Metal 3→4 — must gate/verify selector names against final headers (roadmap risk #3).
- XRAY_EXCEPTIONS=0 on Darwin: a dangling bindless index or referencing a half-loaded cell hits VERIFY (fatal abort), not a catchable throw — so the frame-boundary atomic-publish rule and never-index-an-incomplete-cell invariant are stability-critical, not niceties.
- Steady-state must be zero per-frame allocation (R2 exit criterion): the ICB and residency sets are reused across frames and rebuilt only on cell transition.
- Sparse-texture feedback adds a GPU pass and latency; a cell rounding a corner can outrun tile residency — mitigated by an always-resident low-mip tail (blurry-correct, never a hole), but pop-in to sharp mip is inherent.

**Open questions:** Cell granularity: is a 'cell' exactly one imported CoC level, or should large levels be sub-diced into streaming tiles for finer residency? Sub-dicing means re-cutting baked VB/IB and sectors — how far into the content pipeline are we willing to go in V1 vs V3?; Cross-cell lighting: accept visible seam discontinuity, do a cheap runtime light-list merge at seams, or defer entirely to the R3 RT path (RT shadows could hide baked-shadow seams)? The RT-ready G-buffer (roadmap R1) helps but doesn't fix baked GI seams.; Does the sim's ~30-60 region partition (zone-sim spec §2.1) map 1:1 to render cells, or is a region an aggregation of several render cells? A shared partition keeps render residency and sim LOD coherent but the two were authored for different purposes.; How many cells resident at once on an 8-16GB unified-memory M-series before the working set forces sparse eviction thrash? Needs a memory budget model per M-tier and a hard cap with graceful LOD degrade (mirror the sim's global admission controller).; Seam-portal authoring: derived automatically from old level cross-tables / transition points, or hand-authored? Auto-derivation risks holes/overlap at the seam geometry where two independently-baked cells meet.; Impostor generation: bake far-cell impostor cards offline (extra pipeline step) or render-to-impostor at runtime on first far-view (a hitch, but no pipeline change)?; Does SDL2's present path (roadmap risk #6) interfere with pacing during a residency-change frame, making the one heavier rebuild frame a visible hitch even at steady 0 alloc otherwise?

## Memory budget, performance & honest feasibility verdict for a seamless streamed continuous Zone on unified-memory M-series

Straight answer first: "load the whole map without loading" is achievable as **streaming discrete, pre-baked level-cells that abut**, not as one continuously recompiled world. X-Ray bakes everything per level offline: level.cform (xrCDB/OPCODE collision tree, TRI=16B — src/xrCDB/xrCDB.h:35-57), level.geom render geometry, level.ai (NodeCompressed12=25B/nav cell — src/Common/LevelStructure.hpp:366-424, 25-bit link mask = ~33M nodes/level max), sectors+portals (src/Layers/xrRender/r__sector_detect.cpp), level.hom occlusion (src/Layers/xrRender/HOM.cpp:57), and baked lighting/detail. None of this can be regenerated at runtime cheaply — OPCODE tree build (xrCDB.cpp:131) and lighting bakes are offline-compiler jobs. So the continuous world is a *quilt of baked cells you stream in and out*, stitched at authored seams. Now the memory math. Take a heavy CoC level as the resident unit: render geometry+VBs 150-400MB, textures 200-600MB (this dominates and is shared across cells via the texture registry), xrCDB collision 30-80MB (a few hundred K to ~1M tris × 16B + OPCODE nodes), nav+graph 10-40MB, sound/misc 50MB. Call it ~0.6-1.4GB fully resident for one dense cell today — which is exactly what loading one CoC level costs now. For seamless streaming you hold the player's cell at full LOD plus a ring of neighbors at reduced LOD. A realistic resident set is **1 hot cell + 2-4 adjacent cells** = roughly 1 full + 3×(0.3-0.5 partial) ≈ 2-3GB of cell working set, plus a shared texture pool (~1-1.5GB with aggressive mip-streaming and a global LRU), plus GPU render targets/upscale/RT-AS (~0.5-1GB). The crucial cheap part is the always-on Zone sim: it is *tiny*. The SoA entity store (spec §2.2) is ~40-60B hot/entity; even 50k entities = ~2-3MB, cold side-tables for a few hundred ONLINE/named entities add single-digit MB, and the 30-60 region statistical layer (PopCohorts, Faction ledgers, markets, carcass lists) is well under 10MB. **The whole-Zone sim state is ~10-30MB — a rounding error against textures.** This is the design's biggest win: "the whole Zone is alive" costs almost nothing in RAM because statistical LOD is aggregate math, not entities. Total streaming budget: ~4-6GB working set. That fits comfortably on 24/32/64GB Macs with headroom, is tight-but-viable on **16GB** (you must cap the neighbor ring at 1-2 cells, cap the shared texture pool hard, and accept lower texture LOD on the ring), and is generous on 64GB. The binding constraints are NOT memory — they are the baked per-level seams and two hard u16/24-bit ID caps: game-graph _GRAPH_ID is u16 (game_graph_space.h:63) = **65,536 game-graph vertices ZONE-WIDE total**, and _LEVEL_ID is u8 = 256 levels. A 30-60 region Zone fits under both, but a naive "merge all AI-maps into one giant graph" will blow the u16 cap; the region partition must stay the addressing unit and cross-level edges stay in cross-tables (level.gct). Performance-wise, hitch-free streaming on unified memory is the M-series' home turf: MTLStorageModeShared means a streamed cell's VBs/textures/CDB are GPU-visible with zero staging copies, and the deterministic 100ms sim tick (spec §2.3) is fully decoupled from render via the accumulator, so streaming work rides a background thread and a residency-set swap, never the frame. The honest verdict: **seamless within a region-cluster in V1; sub-second async transitions Zone-wide; true no-screen everywhere is a V3 goal, and even then some seams will be sub-second async loads, not literally zero.** Anyone promising "no loading at all across all 30-60 CoC levels" is overselling — call it hitchless streaming within clusters and sub-100-400ms async handoffs at the hardest seams (big geometry+CDB+nav deltas), and say so plainly.

**Mechanisms:**

- **Cell residency ring + hard budget cap** (1Hz coarse re-eval of resident set on player region change (mirrors spec §2.4 band recompute); prefetch triggered on approach to seam): Player's cell full-LOD; N-hop neighbor ring at reduced LOD (geometry decimated, textures at lower mip, collision kept but simplified proxy where possible). A global memory governor with a fixed byte budget evicts farthest cells LRU. Budget presets: 16GB Mac = 1 hot + 1-2 ring; 24/32GB = 1 + 2-4 ring; 64GB = 1 + 4-6 ring. Textures share one global streaming pool with its own LRU independent of cell residency.
- **Async cell stream on background thread** (Distance-to-seam prefetch; completion swaps atomically): On approaching a seam, kick a background job: mmap/read level.geom+cform+ai for the neighbor, build the OPCODE collision tree (xrCDB.cpp:131 — the one genuinely CPU-heavy step, budget it across frames), upload VBs into MTLStorageModeShared buffers (zero-copy on unified memory), register into MTLResidencySet. Swap it live only when fully built. Never build on the render thread.
- **Runtime CDB stitch, not rebuild** (On cell load: build that cell's tree once; queries fan out to all resident trees): Do NOT merge collision meshes into one giant OPCODE tree at runtime. Keep each cell's baked xrCDB MODEL independent and query the small set of resident trees (the xr_area/ObjectSpace already holds a static model per area — xr_area.cpp:93). A seam is just 'both neighbor trees are resident and queried'. This sidesteps the expensive tree rebuild/merge entirely — accept a thin overlap band at seams rather than a welded mesh.
- **Nav-graph stitching via cross-table edges** (Static at content-import; edges activate when both endpoint cells resident): Reuse the existing cross-level edge machinery (level.gct cross-tables, CGameGraph edges). Keep per-cell level graphs separate (respecting the 24-bit per-level node id) and connect them through pre-authored seam edges in the game graph. Pathfinding (A2's HPA*) treats seam edges as portals between resident subgraphs. Respect the u16 _GRAPH_ID Zone-wide cap — region partition is the addressing layer, not a flattened mega-graph.
- **Always-on statistical sim (the cheap liveness)** (Every 100 ticks (10s) statistical; on cell stream-in, trickled materialize): The whole-Zone sim runs regardless of what's streamed. 30-60 regions × PopCohorts/Faction/market aggregates ≈ <30MB total, ticked at 0.1Hz (ECO_TICK). Cells being unloaded does NOT stop their region simulating — it just means STATISTICAL band, no entities. Materialization (spec §2.5) draws entities from cohorts only when a cell streams in, decrementing the count for conservation. This is why 'the whole Zone alive' is nearly free in memory.
- **Seam occlusion: portal handoff + defer HOM welding** (Static portal placement at seams; per-frame portal traversal as today): HOM (level.hom) is baked per-level and does not weld across seams. V1: treat the seam itself as a portal/occluder boundary (authored) so you never render two full cells' worth of geometry unoccluded. Accept that cross-seam occlusion is conservative (may over-draw a bit at the exact seam). Defer any runtime HOM regeneration — it's not worth it; lean on sectors/portals (r__sector_detect.cpp) and frustum + distance LOD instead.

**Constraints / X-Ray realities:**

- HARD: game-graph _GRAPH_ID is u16 (src/xrAICore/Navigation/game_graph_space.h:63) — max 65,536 game-graph vertices for the ENTIRE Zone. A flattened all-levels-merged graph would overflow this. Keep the region partition + cross-tables as the addressing scheme; do not weld into one graph.
- HARD: _LEVEL_ID is u8 (game_graph_space.h:17) — 256 levels max. Fine for 30-60 CoC levels, but the level-cell is the atomic streaming unit and cannot be subdivided beyond this without ID rework.
- HARD: per-level nav node id is 24/25-bit (tNodeID:24 in game_graph_space.h:79; NodeCompressed12 25-bit links) — ~16-33M cells/level. Not a problem per-cell but forbids one giant continuous nav mesh.
- BAKED-OFFLINE: level.cform (OPCODE tree), level.geom, baked lighting/detail, level.hom are all produced by the offline AI/level compiler. None regenerate cheaply at runtime. 'One continuous world' therefore = streaming baked cells, NOT a runtime world recompile. Lighting/HOM will NOT be seamless across seams without a re-bake pass.
- XRAY_EXCEPTIONS=0 on Darwin (src/CMakeLists.txt): a dangling entity/id reference across a seam must degrade to a value (drop/aggregate), never THROW — a throw is a fatal abort. Two-phase validation at materialize/dissolve (spec §2.5) is mandatory at seam handoff, not optional.
- OPCODE tree Build (xrCDB.cpp:131) is the one CPU-heavy per-cell load step — must be time-sliced across frames on a background thread or it becomes the hitch.
- 16GB Macs are tight: ~4-6GB working set leaves little for OS+other. Cap neighbor ring to 1-2 cells and the shared texture pool hard; expect lower ring-cell texture LOD.
- Textures dominate memory (~200-600MB/dense cell) and are the real budget pressure, NOT the sim (~10-30MB total). Aggressive global mip-streaming with a single LRU pool is the load-bearing optimization.

**Open questions:** What is the actual dense-cell texture/geometry/CDB footprint on real CoC gamedata? No gamedata was present on disk to measure (find level.cform returned nothing). The 0.6-1.4GB/cell figure is an engineering estimate; measure the 3-4 heaviest CoC levels before committing ring sizes.; Do CoC seams line up geometrically enough to stream cell-to-cell, or do transition points teleport across non-adjacent world space (classic STALKER load-point pattern)? If seams are teleports (e.g. underground->surface), 'seamless' is impossible there and it MUST be a sub-second async transition — this needs a per-seam audit.; Can lighting be re-baked or approximated across seams, or do we accept a visible lighting discontinuity at cell boundaries in V1? (Recommend: accept it in V1, revisit with the Metal renderer's own lighting/RT in R3.); What is the OPCODE Build time for a real dense cform on M-series? Determines whether prefetch distance of 1 hop is enough or we need 2. Needs a microbenchmark.; Does the shared texture registry already dedupe across levels, or will streaming two cells double-count shared textures? If it dedupes (likely — shared_str/registry pattern), the ring cost is much lower than the naive sum.; Precise resident-set target for hitchless: is 1+2 enough to fully hide the worst seam's build time, or must we go 1+4 and eat the RAM? Answerable only with the two benchmarks above.


