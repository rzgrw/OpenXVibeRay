# LLM Thin-Harness In-Game AI — Design Spec

**Date:** 2026-07-07
**Status:** Direction approved by rz in chat: "lets make ingame AI thin harness layer around llm".
**Parent:** `2026-07-05-agentic-zone-design.md`
**Supersedes:** The authority boundary in `2026-07-05-agentic-zone-design.md` that kept near-player tactical decisions in deterministic C++ behavior trees. The replay, stable-id, provider, bridge, validation, and coast-on-failure requirements remain.

## 0. Thesis

The in-game AI should be a **thin harness around LLM agents**. C++ is the body: physics, collision, animation, path requests, perception sampling, combat contact resolution, save/load, provider I/O, validation, and hard safety clamps. The LLM is the brain: intent, tactics, fear, memory, faction behavior, mutant pack behavior, off-screen outcomes, dialogue, grudges, curiosity, and narrative continuity.

The old rule was "LLM owns off-screen evolution; C++ owns near-player tactics." The new rule is:

> **LLM owns decisions. C++ owns embodiment.**

No C++ system should author stalker or mutant behavior if that behavior can instead be expressed as an LLM-authored intent and executed safely by the runtime.

## 1. Authority Boundary

### 1.1 xrMind Owns

- Stalker squad tactics: patrol choice, cover intent, flank/retreat/ambush, surrender, negotiation, looting priorities, helping allies, revenge.
- Individual stalker state: fear, trust, grudges, promises, memories of player contact, dialogue posture, personal goals.
- Mutant pack behavior: hunger, territory, stalking, ambush timing, retreat, den movement, response to anomalies/emissions, species pressure.
- Faction strategy: raids, alliances, patrol priorities, trade pressure, retaliation, recruitment, propaganda, quest opportunities.
- Ecology: migration, breeding pressure, predator/prey outcomes, local extinctions, repopulation, anomaly-driven behavior.
- Off-screen fights and hunts: the LLM resolves most outcomes directly as structured state changes, with C++ validating magnitude and conservation.
- Near-player tactical intent: the LLM may decide "pin the player", "circle left", "drag wounded ally away", "break contact", "stalk silently", or "wait until dusk".

### 1.2 C++ Owns

- Physics, collision, hit tests, ballistics, line of sight, pathfinding requests, animation transitions, ragdolls, inventory mechanics, audio emission plumbing.
- Perception snapshots: what an actor can see/hear/smell/remember at a given decision point.
- Tool validation: schema checks, handle existence, rate limits, spatial legality, conservation, cooldowns, no-throw failure values.
- Execution of LLM intents through low-level actuators: move, look, aim, shoot, crouch, reload, flee, follow, attack, vocalize, loot, use item, spawn/materialize.
- Runtime safety: if the LLM is late, invalid, offline, or over budget, actors coast on last valid intent or fall back to minimal survival/reflex rules.
- Replay ledger: every observation, provider result, accepted/rejected tool call, and coast decision is recorded.

### 1.3 C++ Must Not Own

- Strategic faction choices.
- Ecological direction.
- Squad tactics beyond mechanically executing an intent.
- Mutant pack choices beyond mechanically executing an intent.
- Dialogue posture or long-term memory.
- Off-screen battle results except as a validator or random seed provider when explicitly requested by the LLM.

## 2. Actor Model

### 2.1 Persistent Agent Types

- `ZoneAgent`: orchestrates the whole Zone, detects global tension, spawns scoped work, and keeps long-term continuity.
- `FactionAgent`: owns faction-level motives, enemies, patrol policy, revenge, economics, and quest opportunities.
- `SquadAgent`: owns stalker squad tactics and social behavior. It can delegate individual actions to notable `StalkerAgent`s.
- `StalkerAgent`: exists for named, notable, quest-relevant, or player-touched NPCs. It owns memory, emotion, social posture, and personal action choices.
- `MutantPackAgent`: owns a pack/herd/den, including hunger, territory, stalking, ambush, retreat, reproduction pressure, and response to threats.
- `EcologyAgent`: owns species pressure and region-level animal outcomes.
- `EncounterAgent`: short-lived coordinator for active near-player scenes where several groups interact.

The default unit for cost control is the **group agent** (`SquadAgent` or `MutantPackAgent`). Individual agents are reserved for NPCs the player is likely to recognize or care about.

### 2.2 Observation Packet

Each wake receives a compact packet:

```text
agent_id
scope
time_budget_ms
decision_horizon
current_goal
last_intent
memory_summary
visible_entities
audible_events
known_threats
terrain_affordances
inventory_or_pack_state
faction_or_species_state
player_contact_summary
legal_tools
```

The packet is intentionally experiential. It should read like what that actor knows, not a dump of engine internals.

### 2.3 Intent Packet

The LLM returns structured intent:

```text
xr_intent_v1
goal survive_and_delay_player
stance cautious
duration_ms 3500
action move_to cover_node_12
action suppress target_player burst_short
action shout ally_3 "fall back through the warehouse"
condition if_wounded retreat_to escape_route_2
memory player_used_grenade_aggressively
end
```

The C++ executor does not reinterpret this into a behavior tree. It validates each action and runs it through actuators until the duration expires, a condition fires, or a newer intent replaces it.

## 3. Stalkers

Stalkers should feel like people who are thinking, not C++ state machines wearing dialogue.

### 3.1 Squads

Most stalker behavior is squad-level:

- choose route and posture;
- decide whether to avoid, greet, rob, trade, threaten, ambush, or retreat;
- allocate roles: scout, overwatch, wounded carrier, looter;
- coordinate with faction goals;
- remember player reputation and recent contact;
- produce dialogue intent for barks or conversation.

C++ provides available cover nodes, path costs, ammo/health facts, perceived threats, and legal action tools. The LLM decides the plan.

### 3.2 Individuals

Named and player-touched stalkers get stronger continuity:

- personal memory summary;
- emotional state toward the player and factions;
- promises and grudges;
- wounds, debts, fears, ambitions;
- willingness to break squad orders.

An individual can override a squad intent only through a validated tool call, e.g. `request_break_formation(reason, duration_ms)`. The squad agent receives the result and adapts.

### 3.3 Fights

For fights near the player, C++ owns bullets and bodies; LLM owns choices.

LLM decisions:
- engage, delay, hide, flank, suppress, close distance, negotiate, surrender, retreat;
- decide who covers whom;
- decide whether to risk rescuing a wounded ally;
- decide whether the player is too dangerous;
- decide whether mutants are the bigger threat.

C++ decisions:
- whether a bullet hits;
- whether a path is possible;
- whether an animation can play;
- whether an actor can see/hear a target;
- whether an action violates safety limits.

The engine never waits on the LLM during a frame. Agents maintain rolling intents with short horizons. If a fresh intent is unavailable, the actor continues the last valid intent, then degrades to minimal reflexes such as hold cover, keep distance, or flee from immediate lethal danger.

## 4. Mutants

Mutants are not just population counters. They are LLM-controlled packs with motives that are less verbal but still agentic.

### 4.1 Packs and Dens

`MutantPackAgent` owns:

- hunger and risk tolerance;
- territory and den location;
- stalking vs. direct attack;
- pack cohesion;
- target preference;
- fear of fire, gunfire, anomalies, emissions, or rival mutants;
- migration after repeated losses;
- opportunistic feeding and carcass behavior.

The LLM does not need to speak in human prose. The prompt can ask for compact ethological intent:

```text
pack_goal feed_without_losing_alpha
formation crescent
approach silent_until_20m
target weakest_visible_human
abort_if two_pack_members_down
posture hungry_but_cautious
```

### 4.2 Ecology

The ecology agent resolves off-screen births, deaths, migration, predation, and territorial pressure as structured outcomes. C++ enforces conservation, rate bounds, capacity, and valid routes. The LLM decides the story of the ecosystem.

Example:

```text
outcome predation
region garbage_north
predator blind_dog +4 hunger_down
prey flesh -9
notable alpha_pack learned_avoid_checkpoint
reason dogs used storm noise to raid a flesh nest
```

### 4.3 Near-Player Mutants

Near-player mutants still use C++ locomotion, collision, hit reactions, attack traces, and animation. They should not use C++ behavior trees to decide when to stalk, circle, panic, bluff, or retreat. Those are LLM intents translated into actuator commands.

If the provider is late, a pack repeats its last valid intent. If that expires, minimal reflex keeps it physically plausible: maintain distance, finish current leap/attack, flee from immediate damage, or hold den.

## 5. Tools

The tool API should grow from population-only debug tools into actor intent tools.

### 5.1 Read Tools

- `observe_self`
- `observe_visible_entities`
- `observe_audible_events`
- `observe_cover`
- `observe_routes`
- `observe_group_state`
- `observe_memory`
- `observe_faction_context`
- `observe_ecology_context`

### 5.2 Intent Tools

- `set_goal`
- `set_stance`
- `move_to`
- `look_at`
- `aim_at`
- `fire_pattern`
- `use_item`
- `vocalize`
- `interact`
- `loot`
- `follow`
- `guard`
- `retreat`
- `stalk`
- `ambush`
- `split_group`
- `regroup`
- `request_help`
- `author_memory`

### 5.3 World Outcome Tools

- `resolve_offscreen_fight`
- `resolve_hunt`
- `move_force`
- `set_relation`
- `set_control`
- `adjust_population`
- `set_migration`
- `set_breeding`
- `promote_individual`
- `retire_individual`
- `spawn_event`
- `author_quest`

Every write tool returns `{ok, applied, rejected, reason, suggestion}`. Rejections are feedback to the agent, not crashes.

## 6. Cadence and Latency

The design does not require a network call every frame. It requires that current behavior derives from recent LLM intent.

- Off-screen faction/ecology: seconds to minutes.
- Off-screen fights/hunts: event-triggered, batched where possible.
- Near-player squad/pack: rolling short horizon, usually 1-8 seconds.
- Notable individual in conversation or tense combat: faster wakes, bounded by provider budget.
- Emergency reflex: local C++ only, but limited to physical survival and continuity, not tactical authorship.

The important invariant changes from "no LLM near player" to:

> **No frame may block on the LLM, but frame-visible behavior should be executing LLM-authored intent whenever possible.**

## 7. Coasting and Offline

Offline remains thin. The difference is that coasting preserves the last LLM-authored behavior at every level:

- squad keeps its last plan;
- stalker keeps current personal posture;
- mutant pack keeps current hunt/retreat/stalk behavior;
- faction keeps current campaign plan;
- ecology freezes or continues only already-authored transitions.

When intent expires and no provider is available, the engine falls back to minimal actuator defaults. This is not a second AI. It is a physical continuity layer.

## 8. Replay and Determinism

Replay is more important under the thin-harness model because more behavior comes from the provider.

Record:

- observation packet hash and full debug payload when requested;
- provider request and final assembled response;
- tool calls and validation results;
- coast decisions;
- timing bucket, model, provider, token/cost metadata;
- actuator command stream derived from accepted intent.

Replay serves recorded provider responses and replays validation plus actuator derivation. Physics can remain approximate where the existing engine is approximate, but world-state and accepted intent must match.

## 9. Implementation Order

1. Keep the existing `WorldState`, provider shell, bridge verbs, and HTTP transport.
2. Add `xrMind` agent records: `ZoneAgent`, `SquadAgent`, `MutantPackAgent`, and memory summaries.
3. Replace the current text-only `adjust_population` response with `xr_intent_v1`.
4. Build a small actuator executor for debug actors: move/stance/vocalize/adjust_population.
5. Expose bridge tests that wake a `MutantPackAgent` and `SquadAgent` against fake observations.
6. Add replay for observation -> provider response -> accepted intents.
7. Connect one sandboxed in-game stalker squad or mutant pack to the harness while legacy AI remains available as a fallback.
8. Gradually replace legacy decision code with LLM-authored intent paths.

## 10. Non-Goals

- No local load-bearing model.
- No C++ strategic simulation hidden behind the LLM.
- No C++ behavior-tree authoring for stalker or mutant tactics once an LLM-controlled path exists.
- No frame blocking on provider calls.
- No uncapped live-agent fanout.
- No unvalidated direct memory mutation by model output.

## 11. Acceptance Criteria

- A debug `SquadAgent` can receive an observation, produce an intent, and drive a deterministic actuator command stream.
- A debug `MutantPackAgent` can choose stalk/attack/retreat behavior from a compact observation.
- Invalid provider output coasts or rejects as a value, never throws.
- A recorded provider response replays to the same accepted intent and world-state digest.
- `agent.provider live` continues to report provider/model/transport readiness.
- The old "near-player = deterministic behavior tree" rule is no longer treated as the design target.
