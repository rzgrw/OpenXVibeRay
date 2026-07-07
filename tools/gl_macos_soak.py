#!/usr/bin/env python3
"""GL macOS stability soak harness for OpenXVibeRay.

This tool wraps the existing agent bridge. It intentionally starts with
coarse, reliable signals: bridge state, process RSS, screenshots, logs, and
process exit.
"""

from __future__ import annotations

import argparse
import json
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


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true", help="parse inputs and write a dry-run summary")
    parser.add_argument("--scenario", type=Path, required=True, help="scenario text file")
    parser.add_argument("--artifacts", type=Path, default=REPO_ROOT / "artifacts/gl_macos_soak")
    args = parser.parse_args(argv)

    steps = parse_scenario_file(args.scenario)
    if args.dry_run:
        write_json(
            args.artifacts / "summary.json",
            {
                "dry_run": True,
                "scenario": str(args.scenario),
                "step_count": len(steps),
                "steps": [step.raw for step in steps],
            },
        )
        print(f"dry-run ok: {len(steps)} steps")
        return 0

    print("live execution is added in Task 3")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
