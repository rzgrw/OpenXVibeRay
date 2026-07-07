import json
import tempfile
import unittest
from pathlib import Path

from tools.gl_macos_soak import (
    ScenarioStep,
    parse_scenario_lines,
    parse_state_payload,
    scan_log_text,
    write_json,
)


class ScenarioParserTests(unittest.TestCase):
    def test_parse_ignores_blank_lines_and_comments(self):
        steps = parse_scenario_lines([
            "",
            "# comment",
            "hello",
            "sleep 1.5",
            "cmd main_menu off",
        ])

        self.assertEqual([
            ScenarioStep("bridge", ("hello", ""), "hello"),
            ScenarioStep("sleep", ("1.5",), "sleep 1.5"),
            ScenarioStep("bridge", ("cmd", "main_menu off"), "cmd main_menu off"),
        ], steps)

    def test_parse_rejects_empty_sleep(self):
        with self.assertRaisesRegex(ValueError, "sleep requires seconds"):
            parse_scenario_lines(["sleep"])


class StateParserTests(unittest.TestCase):
    def test_parse_state_payload_keeps_position_value(self):
        parsed = parse_state_payload(
            "scene=game fps=60 frame=123 paused=0 loadscr=0 precache=0 "
            "level=l05_bar pos=1.0,2.0,3.0 hp=0.95 time=456"
        )

        self.assertEqual("game", parsed["scene"])
        self.assertEqual("60", parsed["fps"])
        self.assertEqual("123", parsed["frame"])
        self.assertEqual("1.0,2.0,3.0", parsed["pos"])


class LogScannerTests(unittest.TestCase):
    def test_scan_log_text_reports_fatal_gl_and_lua_lines(self):
        findings = scan_log_text(
            "* harmless\n"
            "! error: GL_INVALID_OPERATION during draw\n"
            "FATAL ERROR: crash\n"
            "lua runtime error: sound_theme.script\n"
        )

        self.assertEqual([
            "! error: GL_INVALID_OPERATION during draw",
            "FATAL ERROR: crash",
            "lua runtime error: sound_theme.script",
        ], findings)

    def test_scan_log_text_ignores_known_noise(self):
        self.assertEqual([], scan_log_text("* normal startup\n~ warning: cached texture\n"))


class SummaryWriterTests(unittest.TestCase):
    def test_write_json_creates_parent_and_stable_json(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "nested" / "summary.json"
            write_json(path, {"b": 2, "a": 1})

            self.assertEqual({"a": 1, "b": 2}, json.loads(path.read_text()))
            self.assertTrue(path.read_text().endswith("\n"))


class DryRunCliTests(unittest.TestCase):
    def test_dry_run_writes_summary(self):
        from tools.gl_macos_soak import main

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            scenario = root / "scenario.txt"
            artifacts = root / "artifacts"
            scenario.write_text("hello\nstate\n")

            rc = main(["--dry-run", "--scenario", str(scenario), "--artifacts", str(artifacts)])

            self.assertEqual(0, rc)
            summary = json.loads((artifacts / "summary.json").read_text())
            self.assertEqual(True, summary["dry_run"])
            self.assertEqual(2, summary["step_count"])
            self.assertEqual(["hello", "state"], summary["steps"])


if __name__ == "__main__":
    unittest.main()
