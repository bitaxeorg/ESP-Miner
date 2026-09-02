#!/usr/bin/env python3

import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import coverage_summary  # noqa: E402


class CoverageSummaryTest(unittest.TestCase):
    def setUp(self) -> None:
        self.report = {
            "files": [
                {
                    "filename": "components/example/covered.c",
                    "line_total": 1,
                    "line_covered": 1,
                    "function_total": 1,
                    "function_covered": 1,
                },
                {
                    "filename": "components/example/uncovered.c",
                    "line_total": 1,
                    "line_covered": 0,
                    "function_total": 1,
                    "function_covered": 0,
                },
                {
                    "filename": "main/platform_only.c",
                    "line_total": 0,
                    "line_covered": 0,
                    "function_total": 0,
                    "function_covered": 0,
                },
            ],
            "line_total": 2,
            "line_covered": 1,
            "line_percent": 50.0,
            "branch_total": 2,
            "branch_covered": 1,
            "branch_percent": 50.0,
        }

    def test_separates_breadth_from_gcovr_depth(self) -> None:
        summary = coverage_summary.summarize(self.report)

        self.assertEqual(
            summary["source_file_breadth"],
            {
                "eligible": 3,
                "instrumented": 2,
                "executed": 1,
                "uninstrumented": 1,
                "instrumented_percent": 200.0 / 3.0,
                "executed_percent": 100.0 / 3.0,
            },
        )
        self.assertEqual(
            summary["instrumented_coverage_depth"]["lines"],
            {"total": 2, "covered": 1, "percent": 50.0},
        )

    def test_breadth_gate_detects_inventory_dilution(self) -> None:
        summary = coverage_summary.summarize(self.report)

        self.assertTrue(coverage_summary.add_breadth_gate(summary, 60.0))
        self.assertFalse(coverage_summary.add_breadth_gate(summary, 67.0))

    def test_full_file_gate_requires_every_line_and_function(self) -> None:
        summary = coverage_summary.summarize(self.report)

        self.assertTrue(
            coverage_summary.add_full_file_gates(
                summary, self.report, ["components/example/covered.c"]
            )
        )
        self.assertFalse(
            coverage_summary.add_full_file_gates(
                summary, self.report, ["components/example/uncovered.c"]
            )
        )

    def test_full_file_gate_rejects_a_missing_file(self) -> None:
        summary = coverage_summary.summarize(self.report)

        with self.assertRaisesRegex(ValueError, "required coverage file is missing"):
            coverage_summary.add_full_file_gates(
                summary, self.report, ["components/example/missing.c"]
            )

    def test_rejects_duplicate_files(self) -> None:
        self.report["files"].append(self.report["files"][0])

        with self.assertRaisesRegex(ValueError, "duplicate file"):
            coverage_summary.summarize(self.report)

    def test_renders_scope_in_text_and_markdown(self) -> None:
        summary = coverage_summary.summarize(self.report)
        coverage_summary.add_breadth_gate(summary, 60.0)
        coverage_summary.add_full_file_gates(
            summary, self.report, ["components/example/covered.c"]
        )

        text = coverage_summary.format_text(summary)
        markdown = coverage_summary.format_markdown(summary)

        self.assertIn("Instrumented by the host suite: 2/3 (66.7%)", text)
        self.assertIn("gated by gcovr", text)
        self.assertIn("lines 1/1, functions 1/1 [PASS]", text)
        self.assertIn("Source-file instrumentation breadth", markdown)
        self.assertIn("components/example/covered.c", markdown)


if __name__ == "__main__":
    unittest.main()
