import json
import tempfile
import unittest
from unittest import mock
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


class RssParserTests(unittest.TestCase):
    def test_parse_ps_rss_output(self):
        from tools.gl_macos_soak import parse_ps_rss_output

        self.assertEqual(123456, parse_ps_rss_output("  RSS\n123456\n"))
        self.assertIsNone(parse_ps_rss_output(""))
        self.assertIsNone(parse_ps_rss_output("RSS\nnot-a-number\n"))


class SummaryAnalysisTests(unittest.TestCase):
    def test_frame_progress_detects_stall(self):
        from tools.gl_macos_soak import longest_frame_stall_sec

        samples = [
            {"t": 0.0, "state": {"frame": "10"}},
            {"t": 1.0, "state": {"frame": "10"}},
            {"t": 2.0, "state": {"frame": "11"}},
            {"t": 7.0, "state": {"frame": "11"}},
        ]

        self.assertEqual(5.0, longest_frame_stall_sec(samples))


class TimeoutHelperTests(unittest.TestCase):
    def test_bounded_sleep_seconds_clamps_to_remaining_deadline(self):
        from tools.gl_macos_soak import bounded_sleep_seconds, process_wait_timeout_seconds

        self.assertEqual((2.0, True), bounded_sleep_seconds(45.0, 2.0))
        self.assertEqual((1.5, False), bounded_sleep_seconds(1.5, 2.0))
        self.assertEqual((0.0, True), bounded_sleep_seconds(1.0, -0.1))

        self.assertEqual((10.0, False), process_wait_timeout_seconds(10.0))
        self.assertEqual((0.0, True), process_wait_timeout_seconds(0.0))
        self.assertEqual((0.0, True), process_wait_timeout_seconds(-1.0))


class SocketWaitTests(unittest.TestCase):
    def test_wait_for_socket_fails_when_process_exits_first(self):
        from tools.gl_macos_soak import wait_for_socket

        class ExitedProcess:
            returncode = 42

            def poll(self):
                return self.returncode

        with tempfile.TemporaryDirectory() as tmp:
            missing_socket = Path(tmp) / "missing.sock"

            with self.assertRaisesRegex(RuntimeError, "process exited before socket appeared: 42"):
                wait_for_socket(missing_socket, 60.0, ExitedProcess())


class LiveCliTests(unittest.TestCase):
    def test_live_cli_fails_nonzero_process_returncode(self):
        from tools.gl_macos_soak import main

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            scenario = root / "scenario.txt"
            artifacts = root / "artifacts"
            scenario.write_text("hello\n")

            with mock.patch("tools.gl_macos_soak.run_scenario") as run_scenario:
                run_scenario.return_value = {
                    "timed_out": False,
                    "exception": "",
                    "bridge_failures": [],
                    "process_returncode": -11,
                }

                rc = main(["--scenario", str(scenario), "--artifacts", str(artifacts)])

            self.assertEqual(1, rc)


if __name__ == "__main__":
    unittest.main()
