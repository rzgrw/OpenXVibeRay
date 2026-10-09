#!/usr/bin/env python3
"""Set up a live in-game session in an empty location with godmode, noclip flight,
and spawned mutant packs + Duty squad whose objective is to fight mutants."""

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
    print(f"Initial state: {state}")

    if "scene=game" not in state:
        print("Loading level l02_garbage...")
        bridge.request("cmd", "start server(radik zagirov - to garbage/single/alife/load) client(localhost)")
        
        deadline = time.time() + 60.0
        while time.time() < deadline:
            time.sleep(2.0)
            ok, state = bridge.request("state")
            if "scene=game" in state:
                print(f"Game loaded! State: {state}")
                break
        
        time.sleep(2.0)
        bridge.request("key", "space tap")
        time.sleep(2.0)
        bridge.request("key", "escape tap")
        time.sleep(1.0)

    print("Backing away from level border into open field...")
    bridge.request("key", "s down")
    time.sleep(3.0)
    bridge.request("key", "s up")
    bridge.request("key", "escape tap")

    print("Enabling godmode and unlimited ammo...")
    bridge.request("cmd", "g_god on")
    bridge.request("cmd", "g_unlimitedammo on")

    print("Purging all default mobs from the level to leave a clean empty location...")
    bridge.request(
        "lua",
        "for id=1,65534 do local o=alife():object(id) if o and o.id~=db.actor:id() and (IsStalker(o) or IsMonster(o)) then alife():release(o, true) end end"
    )
    time.sleep(1.0)
    ok, count = bridge.request(
        "lua",
        "local count=0 for id=1,65534 do local o=alife():object(id) if o and o.id~=db.actor:id() and (IsStalker(o) or IsMonster(o)) then count=count+1 end end return count"
    )
    print(f"Remaining default mobs count: {count}")

    print("\n--- Spawning Mutant Packs ---")
    ok, p1 = bridge.request("agent.pack.spawn", "dog_weak 4 10")
    print(f"Pack 1 (Blind Dogs): {p1}")
    ok, p2 = bridge.request("agent.pack.spawn", "bloodsucker_weak 2 12")
    print(f"Pack 2 (Bloodsuckers): {p2}")
    ok, p3 = bridge.request("agent.pack.spawn", "snork_weak 3 14")
    print(f"Pack 3 (Snorks): {p3}")

    print("\n--- Spawning Duty Squad Tasked to Fight Mutants ---")
    ok, p4 = bridge.request("agent.pack.spawn", "sim_default_duty_2 4 16")
    print(f"Pack 4 (Duty Assault Squad): {p4}")

    ok, plist = bridge.request("agent.pack.list")
    print(f"\nAll Active Session Packs:\n  {plist}\n")

    print("Observing Duty Squad situation & doctrine:")
    ok, obs = bridge.request("agent.pack.observe", "4")
    print(f"{obs}\n")

    print("Calculating AI tactical plan for Duty Squad:")
    ok, wake = bridge.request("agent.pack.wake", "4")
    print(f"{wake}")
    ok, cmds = bridge.request("agent.pack.commands")
    print(f"Actuator Commands: {cmds}\n")

    print("Waking all packs with live LLM provider...")
    for i in range(1, 5):
        bridge.request("agent.pack.wake", f"{i} live")
    time.sleep(1.2)

    for i in range(1, 5):
        ok, res = bridge.request("agent.actor.poll", str(i))
        print(f"Live poll {i} -> {res}")

    print("\nEnabling noclip flight mode and elevating camera...")
    bridge.request("cmd", "g_no_clip")
    bridge.request("key", "space down")
    time.sleep(2.0)
    bridge.request("key", "space up")

    print("Capturing aerial screenshot...")
    bridge.request("shot", "duty_vs_mutants_aerial")
    time.sleep(1.0)

    print("\nSUCCESS: Duty squad and mutant packs are active in the empty location!")
    print("The game is live on screen. You can fly around with WASD and look with your mouse.")


if __name__ == "__main__":
    main()
