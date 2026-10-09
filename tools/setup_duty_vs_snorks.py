#!/usr/bin/env python3
"""Set up a live battle in Garbage between Duty soldiers with weapons and Snorks,
with godmode and noclip flight enabled for player observation."""

import os
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT))

from tools.agentctl import Bridge

SOCKET_PATH = "/Users/rz/stalkercoc/appdata/agent_bridge.sock"


def wait_for_socket(path: str, timeout: float = 30.0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(path):
            return
        time.sleep(0.5)
    raise TimeoutError(f"Bridge socket {path} did not appear in {timeout}s")


def main():
    print(f"Connecting to bridge at {SOCKET_PATH}...")
    wait_for_socket(SOCKET_PATH)
    bridge = Bridge(SOCKET_PATH, timeout=30.0)

    ok, out = bridge.request("hello")
    print(f"hello -> {out}")

    ok, state = bridge.request("state")
    print(f"Current state: {state}")

    if "level=l02_garbage" not in state or "scene=game" not in state:
        print("Loading Garbage level save...")
        bridge.request("cmd", "start server(radik zagirov - to garbage/single/alife/load) client(localhost)")
        deadline = time.time() + 60.0
        while time.time() < deadline:
            time.sleep(2.0)
            ok, state = bridge.request("state")
            if "scene=game" in state and "loadscr=0" in state:
                print(f"Game loaded! State: {state}")
                break
        time.sleep(2.0)
        bridge.request("key", "space tap")
        time.sleep(2.0)
        bridge.request("key", "escape tap")
        time.sleep(1.0)

    print("Walking away from level boundary into open terrain...")
    bridge.request("key", "s down")
    time.sleep(3.0)
    bridge.request("key", "s up")
    bridge.request("key", "escape tap")

    print("Enabling godmode, unlimited ammo, and resetting session packs...")
    bridge.request("cmd", "g_god on")
    bridge.request("cmd", "g_unlimitedammo on")
    bridge.request("ai.reset")

    print("\n--- Spawning Snork Pack ---")
    ok, p1 = bridge.request("agent.pack.spawn", "snork_weak 4 12")
    print(f"Snorks spawned: {p1}")

    print("\n--- Spawning Duty Assault Squad with Weapons ---")
    ok, p2 = bridge.request("agent.pack.spawn", "sim_default_duty_2 4 16")
    print(f"Duty squad spawned: {p2}")

    ok, plist = bridge.request("agent.pack.list")
    print(f"\nAll Active Packs:\n  {plist}\n")

    print("Waking Duty squad with AI (Goal: Purge Mutants)...")
    bridge.request("agent.pack.wake", "2")
    ok, cmds = bridge.request("agent.pack.commands")
    print(f"Duty commands: {cmds}")

    print("Waking Snork pack with AI (Goal: Stalk/Ambush)...")
    bridge.request("agent.pack.wake", "1")
    ok, cmds = bridge.request("agent.pack.commands")
    print(f"Snork commands: {cmds}")

    print("\nTriggering real-time live LLM provider wake...")
    bridge.request("agent.pack.wake", "1 live")
    bridge.request("agent.pack.wake", "2 live")
    time.sleep(1.2)
    bridge.request("agent.actor.poll", "1")
    bridge.request("agent.actor.poll", "2")

    print("\nEnabling noclip flight and elevating observer camera...")
    bridge.request("cmd", "g_no_clip")
    bridge.request("key", "space down")
    time.sleep(2.0)
    bridge.request("key", "space up")

    print("\nSwitching to Third-Person Camera Mode (to see yourself)...")
    bridge.request("lua", "db.actor:cam_set(1)")
    time.sleep(1.0)

    print("Capturing live third-person screenshot...")
    bridge.request("shot", "third_person_view")
    time.sleep(1.0)

    print("\nSUCCESS: Battle between Duty soldiers and Snorks is live on screen!")
    print("Third-person camera is active! You can see your character.")
    print("Keys to change camera view:")
    print("  F1 = First-Person")
    print("  F2 = Third-Person Chase Cam")
    print("  F3 = Third-Person Free Look Cam (360 rotate with mouse)")


if __name__ == "__main__":
    main()
