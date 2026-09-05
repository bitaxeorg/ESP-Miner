#!/usr/bin/env python3
"""Inventory active C tests and validate their host-test eligibility."""

import argparse
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TEST_PATTERN = re.compile(
    r'TEST_CASE\s*\(\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"\s*\)',
    re.MULTILINE,
)
NON_HOST_TAGS = {"qemu-integration", "device-integration", "hardware"}


def strip_comments(source: str) -> str:
    """Remove C comments while preserving literals and source line numbers."""

    output = []
    index = 0
    state = "code"

    while index < len(source):
        current = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""

        if state in {"string", "character"}:
            output.append(current)
            index += 1
            if current == "\\" and following:
                output.append(following)
                index += 1
            elif current == ('"' if state == "string" else "'"):
                state = "code"
        elif state == "code" and current in {'"', "'"}:
            output.append(current)
            index += 1
            state = "string" if current == '"' else "character"
        elif state == "code" and current == "/" and following == "/":
            output.extend("  ")
            index += 2
            state = "line-comment"
        elif state == "code" and current == "/" and following == "*":
            output.extend("  ")
            index += 2
            state = "block-comment"
        elif state == "line-comment" and current == "\n":
            output.append(current)
            index += 1
            state = "code"
        elif state == "block-comment" and current == "*" and following == "/":
            output.extend("  ")
            index += 2
            state = "code"
        elif state in {"line-comment", "block-comment"}:
            output.append("\n" if current == "\n" else " ")
            index += 1
        else:
            output.append(current)
            index += 1

    return "".join(output)


def classification_for_tags(tags: list[str]) -> str:
    matching = NON_HOST_TAGS.intersection(tags)
    if not matching:
        return "host-unit"
    if len(matching) > 1:
        return "conflicting"
    return matching.pop()


def find_tests() -> list[dict[str, object]]:
    tests = []
    for path in sorted(ROOT.glob("components/*/test/test_*.c")):
        source = strip_comments(path.read_text(encoding="utf-8"))
        relative_path = path.relative_to(ROOT).as_posix()
        for match in TEST_PATTERN.finditer(source):
            tags = re.findall(r"\[([^]]+)\]", match.group(2))
            tests.append(
                {
                    "name": match.group(1),
                    "tags": tags,
                    "classification": classification_for_tags(tags),
                    "source": relative_path,
                    "line": source.count("\n", 0, match.start()) + 1,
                }
            )
    return tests


def check_inventory(tests: list[dict[str, object]]) -> list[str]:
    errors = []
    # CMake consumes this same manifest. A path mentioned in CMake comments,
    # diagnostics, or another target must not count as a test registration.
    manifest = (ROOT / "host-tests" / "test_sources.txt").read_text(encoding="utf-8")
    host_sources = {
        source
        for line in manifest.splitlines()
        if (source := line.split("#", 1)[0].strip())
    }
    names: set[str] = set()
    classifications_by_source: dict[str, set[str]] = {}

    for test in tests:
        name = str(test["name"])
        source = str(test["source"])
        classification = str(test["classification"])
        classifications_by_source.setdefault(source, set()).add(classification)

        if name in names:
            errors.append(f"duplicate test name: {name}")
        names.add(name)

        if classification == "host-unit" and source not in host_sources:
            errors.append(f"host-eligible test is not in host-tests/test_sources.txt: {source}")
        if classification != "host-unit" and source in host_sources:
            errors.append(f"host-excluded test is compiled by the host suite: {source}")
        if classification == "conflicting":
            errors.append(f"test has conflicting environment tags: {name}")

    for source, classifications in classifications_by_source.items():
        if len(classifications) > 1:
            errors.append(f"test source mixes environment classifications: {source}")

    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail on an invalid inventory or missing host registration",
    )
    arguments = parser.parse_args()

    tests = find_tests()
    errors = check_inventory(tests)
    if arguments.check:
        if errors:
            for error in errors:
                print(f"ERROR: {error}")
            return 1
        host_count = sum(test["classification"] == "host-unit" for test in tests)
        excluded_count = len(tests) - host_count
        print(
            "Test inventory is valid: "
            f"{host_count} host-eligible, {excluded_count} host-excluded"
        )
        return 0

    print(json.dumps({"tests": tests, "errors": errors}, indent=2))
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
