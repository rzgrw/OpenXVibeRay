# Mutant Pack Session Spawn and LLM Control Design

**Date:** 2026-07-08
**Status:** Approved direction from rz: start replacing legacy AI with mutant packs first, and make packs spawnable during live sessions.
**Parent:** `docs/superpowers/specs/2026-07-07-llm-thin-harness-ai-design.md`

## Goal

Build the first real in-game replacement path where runtime-spawned mutant packs are controlled by xrSim/xrMind intent instead of legacy mutant decision authorship. C++ remains the body: spawning, validation, pathing, physics, animation, combat contact, and emergency continuity. The LLM/thin harness owns pack posture and tactics.

## Scope

This slice targets one live-session mutant pack, not the whole ALife ecosystem. The player or Codex can spawn the pack during a running session through the agent bridge. Spawned pack members are registered into an xrSim `MutantPackAgent` record, can be observed through bridge verbs, can be woken through deterministic or live provider paths, and expose the accepted actuator commands for verification.

Legacy AI remains present as a fallback body layer while the new pack controller is active. The old monster FSM may keep locomotion, animation, hit reaction, and immediate survival behavior working, but it must not be the source of pack-level tactics once an xrSim intent is accepted.

## Recommended Approach

Use a direct online runtime-spawn path first. Add a bridge verb such as:

```text
agent.pack.spawn <section> <count> [radius_m]
```

The verb validates that the game is loaded, the actor exists, the section exists in `pSettings`, and count/radius are within safe bounds. It spawns members around the actor using the existing engine spawn path, then registers a single `MutantPackAgent` whose roster contains the spawned object ids. This keeps the first test loop fast: relaunch, load, spawn, wake, screenshot, inspect commands/logs.

Defer full ALife offline/online group spawning until the pack controller is proven. ALife group integration is the next step because it affects save/load, smart terrain ownership, offline simulation, and cleanup semantics.

## Spawn Semantics

Session-spawned packs are debug-playable but still real game objects. The spawn command must return value errors instead of aborting:

- no loaded game;
- no actor;
- invalid section;
- count outside `1..8`;
- radius outside `1..40`;
- no valid nearby navigation point;
- spawn failure for one member.

Partial spawn failure should clean up or report exact spawned ids. No `THROW`, no unchecked `R_ASSERT`, and no Lua dependency for the core spawn verb.

## Pack Control Semantics

The pack agent owns the group decision:

- stalk, ambush, retreat, regroup, hold den, feed, avoid anomaly, avoid gunfire, pressure wounded target;
- formation and risk tolerance;
- memory facts such as `lost_member_to_player`, `heard_gunfire`, or `player_danger_high`.

C++ converts accepted intent into bounded actuator commands. For the first slice, commands may be observable before they fully steer all in-game movement. The required first in-game proof is that a spawned pack is created, registered, observed, woken, and assigned non-legacy pack intent without crashing.

## Provider and Coasting

Deterministic provider remains the default for tests. Live Sonnet wakes are supported through the existing provider environment and must coast as a normal value on timeout, bad output, or provider unavailability.

Invalid provider output must not corrupt the pack. It should either reject with a reason or coast on the last accepted intent.

## Verification

The first acceptance loop:

1. Unit tests for spawn payload parsing and value errors.
2. Unit tests for pack registration and wake/command output.
3. Build `xr_3da`.
4. Relaunch with `-agent_bridge`.
5. Load into game.
6. Run `agent.pack.spawn dog_weak 3 8` or the first valid CoC dog section found at runtime.
7. Run `agent.pack.observe` and `agent.pack.wake`.
8. Capture screenshot and scan logs.

Known blocker: the current live run crashed in CoC `sound_theme.script:206` because `played_id` was nil during campfire sound update. Verification must either avoid the campfire trigger or add a compatibility guard before claiming a stable live play session.

## Non-Goals

- No full stalker replacement in this slice.
- No full ALife group ownership in this slice.
- No local LLM.
- No frame blocking on provider calls.
- No unvalidated direct mutation of live objects from model text.
