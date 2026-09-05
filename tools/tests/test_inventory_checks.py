#!/usr/bin/env python3

import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import test_inventory  # noqa: E402


class InventoryTest(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        root_patch = patch.object(test_inventory, "ROOT", self.root)
        root_patch.start()
        self.addCleanup(root_patch.stop)

        self.source = "components/example/test/test_example.c"
        self.test_file = self.root / self.source
        self.test_file.parent.mkdir(parents=True)
        self.test_file.write_text('TEST_CASE("example", "[example]") {}\n')
        host = self.root / "host-tests"
        host.mkdir()
        self.manifest = host / "test_sources.txt"
        self.manifest.write_text("")
        # A source mentioned in CMake documentation is not a registration.
        (host / "CMakeLists.txt").write_text(f"# ../{self.source}\n")

    def errors(self) -> list[str]:
        return test_inventory.check_inventory(test_inventory.find_tests())

    def test_active_manifest_entry_registers_test(self) -> None:
        self.manifest.write_text(f"\n  {self.source}  # portable tests\n")

        self.assertEqual([], self.errors())

    def test_commented_out_source_is_not_registered(self) -> None:
        self.manifest.write_text(f"# {self.source}\n")

        errors = self.errors()
        self.assertEqual(1, len(errors))
        self.assertIn("host-eligible test is not in", errors[0])
        self.assertIn(self.source, errors[0])

    def test_commented_out_excluded_source_does_not_fail(self) -> None:
        self.test_file.write_text('TEST_CASE("example", "[hardware][not-on-qemu]") {}\n')
        self.manifest.write_text(f"# {self.source}\n")

        self.assertEqual([], self.errors())

    def test_active_excluded_source_fails(self) -> None:
        self.test_file.write_text('TEST_CASE("example", "[qemu-integration]") {}\n')
        self.manifest.write_text(f"{self.source}\n")

        self.assertEqual(
            [f"host-excluded test is compiled by the host suite: {self.source}"],
            self.errors(),
        )

    def test_registration_requires_an_exact_path(self) -> None:
        self.manifest.write_text(f"{self.source}.disabled\n")

        self.assertEqual(1, len(self.errors()))

    def test_block_comment_marker_in_string_cannot_hide_missing_test(self) -> None:
        self.test_file.write_text(
            'const char *pool_text = "/*";\n'
            'TEST_CASE("example", "[example]") {}\n'
        )

        tests = test_inventory.find_tests()
        self.assertEqual(["example"], [test["name"] for test in tests])
        self.assertEqual(2, tests[0]["line"])
        self.assertEqual(1, len(test_inventory.check_inventory(tests)))

    def test_url_in_test_name_is_not_a_line_comment(self) -> None:
        self.test_file.write_text('TEST_CASE("https://example.test", "[example]") {}\n')

        self.assertEqual(
            ["https://example.test"],
            [test["name"] for test in test_inventory.find_tests()],
        )

    def test_escaped_quotes_and_backslashes_preserve_literal_boundaries(self) -> None:
        self.test_file.write_text(r'''const char *value = "escaped quote: \" /* //";
const char *backslash = "\\";
const char quote = '\'';
const char double_quote = '"';
// TEST_CASE("hidden", "[example]") {}
TEST_CASE("visible", "[example]") {}
''')

        tests = test_inventory.find_tests()
        self.assertEqual(["visible"], [test["name"] for test in tests])
        self.assertEqual(6, tests[0]["line"])

    def test_real_comments_are_removed_without_changing_line_numbers(self) -> None:
        self.test_file.write_text('''/*
TEST_CASE("hidden", "[example]") {}
*/
// TEST_CASE("also hidden", "[example]") {}
TEST_CASE("visible", /* feature */ "[example]") {}
''')

        tests = test_inventory.find_tests()
        self.assertEqual(["visible"], [test["name"] for test in tests])
        self.assertEqual(5, tests[0]["line"])

    def test_literal_markers_do_not_hide_duplicate_names(self) -> None:
        self.test_file.write_text('''const char *text = "/*";
TEST_CASE("example", "[example]") {}
TEST_CASE("example", "[example]") {}
''')
        self.manifest.write_text(f"{self.source}\n")

        self.assertEqual(["duplicate test name: example"], self.errors())


if __name__ == "__main__":
    unittest.main()
