#!/usr/bin/env python3
"""Report and enforce native source-file instrumentation breadth."""

import argparse
import json
import sys
from pathlib import Path


def percentage(covered: int, total: int) -> float:
    return 100.0 * covered / total if total else 0.0


def summarize(report: dict) -> dict:
    """Combine gcovr depth totals with file-level instrumentation breadth."""

    files = report.get("files")
    if not isinstance(files, list) or not files:
        raise ValueError("gcovr summary does not contain a non-empty files list")

    instrumented = []
    executed = []
    uninstrumented = []
    filenames = set()

    for entry in files:
        filename = entry.get("filename")
        line_total = entry.get("line_total")
        line_covered = entry.get("line_covered")
        if not isinstance(filename, str) or not filename:
            raise ValueError("gcovr summary contains an invalid filename")
        if filename in filenames:
            raise ValueError(f"gcovr summary contains duplicate file: {filename}")
        if not isinstance(line_total, int) or not isinstance(line_covered, int):
            raise ValueError(f"gcovr summary contains invalid totals for: {filename}")
        filenames.add(filename)

        if line_total:
            instrumented.append(filename)
        else:
            uninstrumented.append(filename)
        if line_covered:
            executed.append(filename)

    eligible_count = len(files)
    instrumented_count = len(instrumented)
    executed_count = len(executed)

    return {
        "schema_version": 1,
        "source_file_breadth": {
            "eligible": eligible_count,
            "instrumented": instrumented_count,
            "executed": executed_count,
            "uninstrumented": len(uninstrumented),
            "instrumented_percent": percentage(instrumented_count, eligible_count),
            "executed_percent": percentage(executed_count, eligible_count),
        },
        "instrumented_coverage_depth": {
            "lines": {
                "total": report["line_total"],
                "covered": report["line_covered"],
                "percent": report["line_percent"],
            },
            "branches": {
                "total": report["branch_total"],
                "covered": report["branch_covered"],
                "percent": report["branch_percent"],
            },
        },
        "files": {
            "instrumented": sorted(instrumented),
            "executed": sorted(executed),
            "uninstrumented": sorted(uninstrumented),
        },
    }


def add_breadth_gate(summary: dict, minimum_breadth: float) -> bool:
    actual = summary["source_file_breadth"]["instrumented_percent"]
    passed = actual >= minimum_breadth
    summary["breadth_gate"] = {
        "actual_percent": actual,
        "minimum_percent": minimum_breadth,
        "passed": passed,
    }
    return passed


def add_full_file_gates(
    summary: dict,
    report: dict,
    filenames: list[str],
    minimum_branch_percent: float = 0.0,
) -> bool:
    """Require complete line/function coverage and a branch floor for selected files."""

    entries = {entry["filename"]: entry for entry in report["files"]}
    gates = []

    for filename in filenames:
        if filename not in entries:
            raise ValueError(f"required coverage file is missing: {filename}")

        entry = entries[filename]
        metrics = {}
        passed = True
        for name in ("line", "function"):
            total = entry.get(f"{name}_total")
            covered = entry.get(f"{name}_covered")
            if not isinstance(total, int) or total <= 0 or not isinstance(covered, int):
                raise ValueError(
                    f"required coverage file has invalid {name} totals: {filename}"
                )
            metric_passed = covered == total
            metrics[f"{name}s"] = {
                "total": total,
                "covered": covered,
                "percent": percentage(covered, total),
                "passed": metric_passed,
            }
            passed = passed and metric_passed

        branch_total = entry.get("branch_total")
        branch_covered = entry.get("branch_covered")
        if (
            not isinstance(branch_total, int)
            or branch_total < 0
            or not isinstance(branch_covered, int)
        ):
            raise ValueError(
                f"required coverage file has invalid branch totals: {filename}"
            )
        branch_percent = percentage(branch_covered, branch_total)
        branch_passed = branch_percent >= minimum_branch_percent
        metrics["branches"] = {
            "total": branch_total,
            "covered": branch_covered,
            "percent": branch_percent,
            "minimum_percent": minimum_branch_percent,
            "passed": branch_passed,
        }
        passed = passed and branch_passed

        gates.append({"filename": filename, **metrics, "passed": passed})

    summary["full_file_gates"] = gates
    return all(gate["passed"] for gate in gates)


def format_text(summary: dict) -> str:
    breadth = summary["source_file_breadth"]
    lines = summary["instrumented_coverage_depth"]["lines"]
    branches = summary["instrumented_coverage_depth"]["branches"]
    gate = summary["breadth_gate"]
    status = "PASS" if gate["passed"] else "FAIL"

    output = [
        "ESP-Miner native coverage summary",
        "",
        "Source-file instrumentation breadth:",
        f"  Eligible first-party production files: {breadth['eligible']}",
        (
            "  Instrumented by the host suite: "
            f"{breadth['instrumented']}/{breadth['eligible']} "
            f"({breadth['instrumented_percent']:.1f}%)"
        ),
        (
            "  Executed by the host suite: "
            f"{breadth['executed']}/{breadth['eligible']} "
            f"({breadth['executed_percent']:.1f}%)"
        ),
        f"  Uninstrumented: {breadth['uninstrumented']}",
        f"  Breadth floor: {gate['minimum_percent']:.1f}% [{status}]",
        "",
        "Coverage depth within instrumented files (gated by gcovr):",
        f"  Lines: {lines['percent']:.1f}% ({lines['covered']}/{lines['total']})",
        (
            f"  Branches: {branches['percent']:.1f}% "
            f"({branches['covered']}/{branches['total']})"
        ),
    ]

    if summary.get("full_file_gates"):
        output.extend(["", "Required file coverage:"])
        for file_gate in summary["full_file_gates"]:
            status = "PASS" if file_gate["passed"] else "FAIL"
            output.append(
                f"  {file_gate['filename']}: "
                f"lines {file_gate['lines']['covered']}/{file_gate['lines']['total']}, "
                f"functions {file_gate['functions']['covered']}/{file_gate['functions']['total']} "
                f"(both 100%); branches {file_gate['branches']['percent']:.1f}% "
                f"(floor {file_gate['branches']['minimum_percent']:.1f}%) "
                f"[{status}]"
            )

    output.append("")
    return "\n".join(output)


def format_markdown(summary: dict) -> str:
    breadth = summary["source_file_breadth"]
    lines = summary["instrumented_coverage_depth"]["lines"]
    branches = summary["instrumented_coverage_depth"]["branches"]
    gate = summary["breadth_gate"]
    status = "PASS" if gate["passed"] else "FAIL"

    output = [
        "### ESP-Miner native coverage",
        "",
        "| Metric | Result |",
        "| --- | ---: |",
        (
            "| Source-file instrumentation breadth | "
            f"{breadth['instrumented']}/{breadth['eligible']} "
            f"({breadth['instrumented_percent']:.1f}%; "
            f"floor {gate['minimum_percent']:.1f}%) {status} |"
        ),
        (
            "| Line coverage within instrumented files | "
            f"{lines['covered']}/{lines['total']} ({lines['percent']:.1f}%) |"
        ),
        (
            "| Branch coverage within instrumented files | "
            f"{branches['covered']}/{branches['total']} "
            f"({branches['percent']:.1f}%) |"
        ),
    ]

    for file_gate in summary.get("full_file_gates", []):
        status = "PASS" if file_gate["passed"] else "FAIL"
        output.append(
            f"| Required: `{file_gate['filename']}` | "
            f"{file_gate['lines']['covered']}/{file_gate['lines']['total']} lines; "
            f"{file_gate['functions']['covered']}/{file_gate['functions']['total']} functions; "
            f"{file_gate['branches']['percent']:.1f}% branches "
            f"(floor {file_gate['branches']['minimum_percent']:.1f}%) "
            f"{status} |"
        )

    output.extend(
        [
            "",
            (
                f"Executed files: {breadth['executed']}/{breadth['eligible']}; "
                f"uninstrumented files: {breadth['uninstrumented']}."
            ),
            "",
        ]
    )
    return "\n".join(output)


def parse_percentage(value: str) -> float:
    parsed = float(value)
    if not 0.0 <= parsed <= 100.0:
        raise argparse.ArgumentTypeError("percentage must be between 0 and 100")
    return parsed


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("report", type=Path, help="gcovr JSON summary")
    parser.add_argument("--text-output", required=True, type=Path)
    parser.add_argument("--json-output", required=True, type=Path)
    parser.add_argument("--markdown-output", required=True, type=Path)
    parser.add_argument("--fail-under-breadth", required=True, type=parse_percentage)
    parser.add_argument(
        "--require-fully-covered-file",
        action="append",
        default=[],
        help="require 100%% line and function coverage for this report path",
    )
    parser.add_argument(
        "--required-file-branch-floor",
        type=parse_percentage,
        default=0.0,
        help="minimum branch coverage for every required file",
    )
    arguments = parser.parse_args()

    try:
        report = json.loads(arguments.report.read_text(encoding="utf-8"))
        summary = summarize(report)
        breadth_passed = add_breadth_gate(summary, arguments.fail_under_breadth)
        files_passed = add_full_file_gates(
            summary,
            report,
            arguments.require_fully_covered_file,
            arguments.required_file_branch_floor,
        )
    except (KeyError, OSError, json.JSONDecodeError, ValueError) as error:
        print(f"ERROR: unable to summarize coverage: {error}", file=sys.stderr)
        return 2

    text = format_text(summary)
    arguments.text_output.write_text(text, encoding="utf-8")
    arguments.json_output.write_text(
        json.dumps(summary, indent=2) + "\n", encoding="utf-8"
    )
    arguments.markdown_output.write_text(format_markdown(summary), encoding="utf-8")
    print(text, end="")

    if not breadth_passed:
        actual = summary["breadth_gate"]["actual_percent"]
        print(
            f"ERROR: source-file instrumentation breadth is {actual:.1f}%, "
            f"below the {arguments.fail_under_breadth:.1f}% floor",
            file=sys.stderr,
        )
    for file_gate in summary["full_file_gates"]:
        if not file_gate["passed"]:
            print(
                f"ERROR: {file_gate['filename']} does not have 100% line and "
                "function coverage or meet its branch floor",
                file=sys.stderr,
            )

    return 0 if breadth_passed and files_passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
