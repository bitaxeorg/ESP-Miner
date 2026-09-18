#!/usr/bin/env python3

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
PASSING_LOG = "1 Tests 0 Failures 0 Ignored\nOK\n"


class QemuRunnerTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        (self.root / "tools").mkdir()
        (self.root / "test-ci").mkdir()
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.runner = self.root / "tools/run_qemu_tests.sh"
        shutil.copyfile(ROOT / "tools/run_qemu_tests.sh", self.runner)
        self.record = self.root / "build-args.json"
        self.make_tool("idf.py", """
import json, os, pathlib, sys
pathlib.Path(os.environ['QEMU_TEST_BUILD_ARGS']).write_text(json.dumps(sys.argv[1:]))
build = pathlib.Path(sys.argv[sys.argv.index('-B') + 1])
build.mkdir(parents=True, exist_ok=True)
(build / 'flash_args').write_text('')
""")
        self.make_tool("esptool", "")
        self.make_tool("qemu-system-xtensa", """
import os, pathlib, sys
if '--version' in sys.argv:
    print('QEMU test stub')
else:
    output = sys.argv[sys.argv.index('-serial') + 1].removeprefix('file:')
    if os.environ['QEMU_TEST_WRITE_LOG'] == '1':
        pathlib.Path(output).write_text(os.environ['QEMU_TEST_LOG'])
    sys.exit(int(os.environ['QEMU_TEST_STATUS']))
""")

    def make_tool(self, name, body):
        path = self.bin / name
        path.write_text(f"#!{sys.executable}\n" + body)
        path.chmod(0o755)

    def run_runner(self, *args, log=PASSING_LOG, status=0, build_dir=None):
        env = os.environ.copy()
        env.pop("ESP_QEMU_BUILD_DIR", None)
        env.update(
            PATH=f"{self.bin}{os.pathsep}{env['PATH']}",
            QEMU_TEST_BUILD_ARGS=str(self.record),
            QEMU_TEST_LOG=log or "",
            QEMU_TEST_WRITE_LOG="0" if log is None else "1",
            QEMU_TEST_STATUS=str(status),
        )
        if build_dir is not None:
            env["ESP_QEMU_BUILD_DIR"] = str(build_dir)
        return subprocess.run(
            ["bash", str(self.runner), *args], env=env,
            capture_output=True, text=True, timeout=15,
        )

    def test_default_and_ubsan_use_separate_builds_and_configs(self):
        configurations = []
        for args, setting in [((), "OFF"), (("--ubsan",), "ON")]:
            with self.subTest(setting=setting):
                result = self.run_runner(*args)
                self.assertEqual(0, result.returncode, result.stderr)
                command = json.loads(self.record.read_text())
                self.assertIn(f"ENABLE_UBSAN={setting}", command)
                configurations.append((
                    command[command.index("-B") + 1],
                    next(arg for arg in command if arg.startswith("SDKCONFIG=")),
                ))
        self.assertNotEqual(configurations[0][0], configurations[1][0])
        self.assertNotEqual(configurations[0][1], configurations[1][1])

    def test_build_override_keeps_mode_configs_separate(self):
        configs = []
        build = self.root / "custom build"
        for args in [(), ("--ubsan",), ()]:
            result = self.run_runner(*args, build_dir=build)
            self.assertEqual(0, result.returncode, result.stderr)
            command = json.loads(self.record.read_text())
            self.assertEqual(str(build), command[command.index("-B") + 1])
            configs.append(next(arg for arg in command if arg.startswith("SDKCONFIG=")))
        self.assertNotEqual(configs[0], configs[1])
        self.assertEqual(configs[0], configs[2])

    def test_test_failures_and_missing_summary_fail(self):
        for log in ["1 Tests 1 Failures 0 Ignored\nFAIL\n", "Booted, then crashed\n", None]:
            with self.subTest(log=log):
                self.assertNotEqual(0, self.run_runner(log=log).returncode)

    def test_ubsan_fails_even_after_a_passing_summary(self):
        for prefix in ["", PASSING_LOG]:
            with self.subTest(prefix=prefix):
                log = prefix + "Undefined behavior of type add_overflow\n"
                result = self.run_runner("--ubsan", log=log)
                self.assertNotEqual(0, result.returncode)
                self.assertIn("UBSan detected undefined behavior", result.stderr)

    def test_qemu_errors_and_timeouts_preserve_serial_output(self):
        for status in [1, 124]:
            for log in [PASSING_LOG, None]:
                with self.subTest(status=status, log=log):
                    result = self.run_runner(log=log, status=status)
                    self.assertEqual(status, result.returncode)
                    if log is not None:
                        self.assertIn(log, result.stdout)

    def test_help_and_bad_arguments_do_not_build(self):
        for args, status in [(("--help",), 0), (("--unknown",), 2), (("--ubsan", "extra"), 2)]:
            with self.subTest(args=args):
                result = self.run_runner(*args)
                self.assertEqual(status, result.returncode)
                self.assertIn("Usage:", result.stdout + result.stderr)
                self.assertFalse(self.record.exists())


if __name__ == "__main__":
    unittest.main()
