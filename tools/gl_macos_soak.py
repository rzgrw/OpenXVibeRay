#!/usr/bin/env python3
"""GL macOS stability soak harness for OpenXVibeRay.

This tool wraps the existing agent bridge. It intentionally starts with
coarse, reliable signals: bridge state, process RSS, screenshots, logs, and
process exit.
"""

from __future__ import annotations

import argparse
import json
import socket
import subprocess
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_GAME_DIR = Path("/Users/rz/Downloads/S.T.A.L.K.E.R. - Call of Chernobyl")
DEFAULT_BINARY = REPO_ROOT / "bin/arm64/Release/xr_3da"
DEFAULT_SOCKET_REL = Path("appdata/agent_bridge.sock")
DEFAULT_LOG_REL = Path("appdata/logs/openxray_radik zagirov.log")


@dataclass(frozen=True)
class ScenarioStep:
    kind: str
    args: tuple[str, ...]
    raw: str


def parse_scenario_lines(lines: Iterable[str]) -> list[ScenarioStep]:
    steps: list[ScenarioStep] = []
    for line_no, raw in enumerate(lines, start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        verb, sep, payload = line.partition(" ")
        if verb == "sleep":
            if not sep or not payload.strip():
                raise ValueError(f"line {line_no}: sleep requires seconds")
            seconds = payload.strip()
            float(seconds)
            steps.append(ScenarioStep("sleep", (seconds,), line))
            continue
        steps.append(ScenarioStep("bridge", (verb, payload.strip()), line))
    return steps


def parse_scenario_file(path: Path) -> list[ScenarioStep]:
    return parse_scenario_lines(path.read_text().splitlines())


def parse_state_payload(payload: str) -> dict[str, str]:
    parsed: dict[str, str] = {}
    for token in payload.split():
        key, sep, value = token.partition("=")
        if sep:
            parsed[key] = value
    return parsed


def scan_log_text(text: str) -> list[str]:
    needles = (
        "FATAL",
        "fatal",
        "GL_INVALID",
        "OpenGL error",
        "lua runtime error",
        "stack traceback",
    )
    findings: list[str] = []
    for raw in text.splitlines():
        line = raw.strip()
        if any(needle in line for needle in needles):
            findings.append(line)
    return findings


def write_json(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")


class BridgeClient:
    def __init__(self, path: Path, timeout: float = 15.0):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(timeout)
        self.sock.connect(str(path))
        self.buf = b""
        self.next_id = 1

    def close(self) -> None:
        self.sock.close()

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
                    continue
                ok = len(parts) > 1 and parts[1] == "ok"
                payload_out = parts[2] if len(parts) > 2 else ""
                return ok, payload_out.replace("\\n", "\n")
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("bridge closed the connection")
            self.buf += chunk


def parse_ps_rss_output(output: str) -> int | None:
    for raw in output.splitlines():
        line = raw.strip()
        if not line or line == "RSS":
            continue
        try:
            return int(line)
        except ValueError:
            return None
    return None


def sample_rss_kb(pid: int) -> int | None:
    proc = subprocess.run(
        ["ps", "-o", "rss=", "-p", str(pid)],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    )
    return parse_ps_rss_output(proc.stdout)


def longest_frame_stall_sec(samples: list[dict[str, Any]]) -> float:
    longest = 0.0
    last_frame: int | None = None
    last_change_t: float | None = None
    for sample in samples:
        state = sample.get("state", {})
        try:
            frame = int(state.get("frame", ""))
        except ValueError:
            continue
        t = float(sample["t"])
        if last_frame is None or frame != last_frame:
            last_frame = frame
            last_change_t = t
            continue
        if last_change_t is not None:
            longest = max(longest, t - last_change_t)
    return longest


def bounded_sleep_seconds(requested_seconds: float, remaining_seconds: float) -> tuple[float, bool]:
    if remaining_seconds <= 0.0:
        return 0.0, True
    if requested_seconds > remaining_seconds:
        return remaining_seconds, True
    return requested_seconds, False


def process_wait_timeout_seconds(remaining_seconds: float) -> tuple[float, bool]:
    if remaining_seconds <= 0.0:
        return 0.0, True
    return min(15.0, remaining_seconds), False


def wait_for_socket(path: Path, timeout: float, process: subprocess.Popen[Any] | None = None) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.exists():
            return
        if process is not None and process.poll() is not None:
            raise RuntimeError(f"process exited before socket appeared: {process.returncode}")
        time.sleep(0.1)
    raise TimeoutError(f"socket did not appear: {path}")


def run_scenario(args: argparse.Namespace, steps: list[ScenarioStep]) -> dict[str, Any]:
    game_dir = args.game_dir
    socket_path = args.socket if args.socket.is_absolute() else game_dir / args.socket
    log_path = game_dir / DEFAULT_LOG_REL
    artifacts = args.artifacts
    artifacts.mkdir(parents=True, exist_ok=True)

    if log_path.exists():
        log_path.write_text("")
    if socket_path.exists():
        socket_path.unlink()

    command = [str(args.binary), "-agent_bridge", str(socket_path)]
    started = time.monotonic()
    deadline = started + args.timeout
    proc = subprocess.Popen(
        command,
        cwd=str(game_dir),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        start_new_session=True,
    )

    states: list[dict[str, Any]] = []
    rss_samples: list[dict[str, Any]] = []
    bridge_failures: list[dict[str, str]] = []
    stop_sampling = threading.Event()

    def sampler() -> None:
        while not stop_sampling.is_set():
            t = time.monotonic() - started
            rss = sample_rss_kb(proc.pid)
            if rss is not None:
                rss_samples.append({"t": round(t, 3), "rss_kb": rss})
            time.sleep(args.sample_interval)

    sample_thread = threading.Thread(target=sampler, daemon=True)
    sample_thread.start()
    bridge: BridgeClient | None = None
    timed_out = False
    exception_text = ""

    try:
        wait_for_socket(socket_path, args.timeout, proc)
        bridge = BridgeClient(socket_path, timeout=args.timeout)
        for step in steps:
            remaining = deadline - time.monotonic()
            if remaining <= 0.0:
                timed_out = True
                break
            if step.kind == "sleep":
                sleep_seconds, clipped = bounded_sleep_seconds(float(step.args[0]), remaining)
                if sleep_seconds > 0.0:
                    time.sleep(sleep_seconds)
                if clipped:
                    timed_out = True
                    break
                continue
            verb, payload = step.args
            ok, out = bridge.request(verb, payload)
            if verb == "state" and ok:
                states.append({
                    "t": round(time.monotonic() - started, 3),
                    "raw": out,
                    "state": parse_state_payload(out),
                })
            if not ok:
                bridge_failures.append({"command": step.raw, "response": out})
        wait_timeout, already_timed_out = process_wait_timeout_seconds(deadline - time.monotonic())
        if already_timed_out:
            timed_out = True
        else:
            try:
                proc.wait(timeout=wait_timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
    except Exception as exc:
        exception_text = str(exc)
    finally:
        stop_sampling.set()
        sample_thread.join(timeout=2.0)
        if bridge is not None:
            bridge.close()
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5.0)

    log_text = log_path.read_text(errors="replace") if log_path.exists() else ""
    log_copy = artifacts / "openxray.log"
    log_copy.write_text(log_text)

    duration_sec = round(time.monotonic() - started, 3)
    summary = {
        "dry_run": False,
        "scenario": str(args.scenario),
        "game_dir": str(game_dir),
        "binary": str(args.binary),
        "socket": str(socket_path),
        "command": command,
        "duration_sec": duration_sec,
        "process_returncode": proc.returncode,
        "timed_out": timed_out,
        "exception": exception_text,
        "states": states,
        "rss_samples": rss_samples,
        "rss_high_water_kb": max((s["rss_kb"] for s in rss_samples), default=None),
        "bridge_failures": bridge_failures,
        "longest_frame_stall_sec": longest_frame_stall_sec(states),
        "log_findings": scan_log_text(log_text),
        "log_copy": str(log_copy),
    }
    write_json(artifacts / "summary.json", summary)
    return summary


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true", help="parse inputs and write a dry-run summary")
    parser.add_argument("--scenario", type=Path, required=True, help="scenario text file")
    parser.add_argument("--artifacts", type=Path, default=REPO_ROOT / "artifacts/gl_macos_soak")
    parser.add_argument("--game-dir", type=Path, default=DEFAULT_GAME_DIR)
    parser.add_argument("--binary", type=Path, default=DEFAULT_BINARY)
    parser.add_argument("--socket", type=Path, default=DEFAULT_SOCKET_REL)
    parser.add_argument("--sample-interval", type=float, default=1.0)
    parser.add_argument("--timeout", type=float, default=900.0)
    args = parser.parse_args(argv)

    steps = parse_scenario_file(args.scenario)
    if args.dry_run:
        write_json(args.artifacts / "summary.json", {
            "dry_run": True,
            "scenario": str(args.scenario),
            "step_count": len(steps),
            "steps": [step.raw for step in steps],
        })
        print(f"dry-run ok: {len(steps)} steps")
        return 0

    summary = run_scenario(args, steps)
    print(f"summary: {args.artifacts / 'summary.json'}")
    if summary["timed_out"] or summary["exception"] or summary["bridge_failures"] or summary["process_returncode"] != 0:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
