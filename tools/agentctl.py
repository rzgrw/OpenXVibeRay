#!/usr/bin/env python3
"""Agent-bridge client for OpenXVibeRay. Protocol v1 (see
docs/superpowers/specs/2026-07-04-agent-bridge-design.md)."""
import socket
import sys
import time


class Bridge:
    def __init__(self, path: str, timeout: float = 15.0):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(timeout)
        self.sock.connect(path)
        self.buf = b""
        self.next_id = 1

    def request(self, verb: str, payload: str = "") -> tuple[bool, str]:
        rid = str(self.next_id)
        self.next_id += 1
        line = f"{rid} {verb} {payload}".strip() + "\n"
        self.sock.sendall(line.encode())
        while True:
            nl = self.buf.find(b"\n")
            if nl >= 0:
                resp = self.buf[:nl].decode()
                self.buf = self.buf[nl + 1:]
                parts = resp.split(" ", 2)
                if parts[0] != rid:
                    continue  # stale response from a previous client — skip
                ok = len(parts) > 1 and parts[1] == "ok"
                payload_out = parts[2] if len(parts) > 2 else ""
                return ok, payload_out.replace("\\n", "\n")
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("bridge closed the connection")
            self.buf += chunk


def run_script(bridge: Bridge, path: str) -> int:
    failures = 0
    for raw in open(path):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("sleep "):
            time.sleep(float(line.split()[1]))
            continue
        verb, _, payload = line.partition(" ")
        ok, out = bridge.request(verb, payload)
        status = "ok " if ok else "ERR"
        print(f"[{status}] {line} -> {out}")
        if not ok:
            failures += 1
    return failures


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        print("usage: agentctl.py <socket> <verb> [payload...] | <socket> --script <file>")
        return 2
    bridge = Bridge(sys.argv[1])
    if sys.argv[2] == "--script":
        return 1 if run_script(bridge, sys.argv[3]) else 0
    ok, out = bridge.request(sys.argv[2], " ".join(sys.argv[3:]))
    print(out)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
