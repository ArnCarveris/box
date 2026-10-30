#!/usr/bin/env python3
"""Unit tests for the bake3 environment lookup of integration.py. Run with:
python3 tools/test_integration.py [--json report.json]"""
import argparse
import datetime as dt
import json
import os
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import integration

ROOT = integration.ROOT


class BakeSettingsTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.home = os.path.join(self.dir.name, "bakehome")
        os.makedirs(self.home)
        self.environ = {"HOME": self.dir.name, "BAKE_HOME": self.home}

    def tearDown(self):
        self.dir.cleanup()

    def write_settings(self, text):
        with open(os.path.join(self.home, "bake.json"), "w") as f:
            f.write(text)

    def test_home_default(self):
        self.assertEqual(integration.bake_home({"HOME": "/h"}), "/h/bake3")

    def test_home_absolute(self):
        self.assertEqual(integration.bake_home(self.environ), self.home)

    def test_home_relative_to_cwd(self):
        cwd = os.path.join(self.dir.name, "a", "b")
        os.makedirs(cwd)
        self.assertEqual(integration.bake_home({"BAKE_HOME": "env"}, cwd),
                         os.path.join(cwd, "env"))

    def test_home_relative_outermost_ancestor(self):
        cwd = os.path.join(self.dir.name, "a", "b")
        os.makedirs(cwd)
        os.makedirs(os.path.join(self.dir.name, "a", "env"))
        self.assertEqual(integration.bake_home({"BAKE_HOME": "env"}, cwd),
                         os.path.join(self.dir.name, "a", "env"))

    def test_home_local_env_uses_global_home(self):
        environ = {"BAKE_LOCAL_ENV": "1", "BAKE_HOME": "/local", "BAKE_GLOBAL_HOME": "/global"}
        self.assertEqual(integration.bake_home(environ), "/global")

    def test_no_settings(self):
        self.assertIsNone(integration.bake_output_dir("box", self.environ))

    def test_settings_without_output_dir(self):
        self.write_settings('{"colors": false}')
        self.assertIsNone(integration.bake_output_dir("box", self.environ))

    def test_output_dir_id_underscore(self):
        self.write_settings('{"output_dir": "/tmp/out/${id underscore}"}')
        self.assertEqual(integration.bake_output_dir("foo.bar/baz", self.environ),
                         "/tmp/out/foo_bar_baz")

    def test_output_dir_id(self):
        self.write_settings('{"output_dir": "/tmp/out/${id}"}')
        self.assertEqual(integration.bake_output_dir("foo.bar", self.environ), "/tmp/out/foo.bar")

    def test_output_dir_env(self):
        self.environ["OUT"] = "/scratch"
        self.write_settings('{"output_dir": "${env OUT}/${id}"}')
        self.assertEqual(integration.bake_output_dir("box", self.environ), "/scratch/box")

    def test_output_dir_tilde(self):
        self.write_settings('{"output_dir": "~/out/${id}"}')
        self.assertEqual(integration.bake_output_dir("box", self.environ),
                         os.path.join(self.dir.name, "out", "box"))

    def test_output_dir_relative_to_settings(self):
        self.write_settings('{"output_dir": "out/${id}"}')
        self.assertEqual(integration.bake_output_dir("box", self.environ),
                         os.path.join(self.home, "out", "box"))

    def test_settings_with_comments(self):
        self.write_settings('{\n  // redirected\n  "output_dir": "/tmp/a//${id}" /* x */\n}')
        self.assertEqual(integration.bake_output_dir("box", self.environ), "/tmp/a/box")

    def assert_settings_error(self, text, fragment):
        self.write_settings(text)
        with self.assertRaises(integration.BakeSettingsError) as ctx:
            integration.bake_output_dir("box", self.environ)
        message = str(ctx.exception)
        self.assertIn(os.path.join(self.home, "bake.json"), message)
        self.assertIn(fragment, message)

    def test_unparsable_settings(self):
        self.assert_settings_error('{"output_dir": ', "cannot parse")

    def test_settings_not_object(self):
        self.assert_settings_error('[]', "expected a JSON object")

    def test_output_dir_not_string(self):
        self.assert_settings_error('{"output_dir": 3}', "non-empty string")

    def test_unsupported_expression(self):
        self.assert_settings_error('{"output_dir": "/tmp/${name}"}', "unsupported expression")

    def test_unsupported_id_modifier(self):
        self.assert_settings_error('{"output_dir": "/tmp/${id upper}"}', "unsupported expression")

    def test_unset_env(self):
        self.assert_settings_error('{"output_dir": "/tmp/${env NOPE}/${id}"}', "NOPE")

    def test_unterminated_expression(self):
        self.assert_settings_error('{"output_dir": "/tmp/${id"}', "unterminated")

    def test_output_dir_without_id(self):
        self.assert_settings_error('{"output_dir": "/tmp/out"}', "${id}")

    def test_local_env_dirs_default(self):
        dirs = integration.local_env_dirs(None, self.environ)
        self.assertEqual(dirs[0], os.path.join(ROOT, ".bake", "local_env"))

    def test_local_env_dirs_output_dir(self):
        self.write_settings('{"output_dir": "%s/${id underscore}"}' % self.dir.name)
        dirs = integration.local_env_dirs("review", self.environ)
        self.assertEqual(dirs[0], os.path.join(self.dir.name, "box", "local_env", "review"))

    def test_app_binary_in_output_dir(self):
        self.write_settings('{"output_dir": "%s/${id underscore}"}' % self.dir.name)
        dirs = integration.local_env_dirs(None, self.environ)
        build = os.path.join(dirs[0], "build", "oreelse", "x64-Linux-debug")
        os.makedirs(build)
        open(os.path.join(build, "oreelse"), "w").close()
        args = argparse.Namespace(app="apps/oreelse", cfg=None)
        binary, searched = integration.app_binary(args, dirs)
        self.assertEqual(binary, os.path.join(build, "oreelse"))
        self.assertEqual(searched[0], os.path.join(dirs[0], "build", "oreelse", "*-debug"))

    def test_app_binary_missing_lists_searched(self):
        dirs = [os.path.join(self.dir.name, "x"), os.path.join(self.dir.name, "y")]
        args = argparse.Namespace(app="oreelse", cfg="release")
        binary, searched = integration.app_binary(args, dirs)
        self.assertIsNone(binary)
        self.assertEqual(searched, [os.path.join(d, "build", "oreelse", "*-release")
                                    for d in dirs])


class JsonResult(unittest.TextTestResult):
    def record(self, test, status):
        if not hasattr(self, "records"):
            self.records = []
        self.records.append({"suite": type(test).__name__, "case": test._testMethodName,
                             "status": status})

    def addSuccess(self, test):
        super().addSuccess(test)
        self.record(test, "pass")

    def addFailure(self, test, err):
        super().addFailure(test, err)
        self.record(test, "fail")

    def addError(self, test, err):
        super().addError(test, err)
        self.record(test, "fail")


def main():
    json_path = None
    argv = list(sys.argv)
    if "--json" in argv:
        i = argv.index("--json")
        json_path = argv[i + 1]
        del argv[i:i + 2]
    start = time.monotonic()
    runner = unittest.TextTestRunner(resultclass=JsonResult, verbosity=2)
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(BakeSettingsTests)
    result = runner.run(suite)
    if json_path:
        records = sorted(getattr(result, "records", []), key=lambda r: r["case"])
        report = {
            "project": "integration",
            "timestamp": dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "pass": sum(r["status"] == "pass" for r in records),
            "fail": sum(r["status"] == "fail" for r in records),
            "empty": 0,
            "elapsed": time.monotonic() - start,
            "tests": records,
        }
        with open(json_path, "w") as f:
            json.dump(report, f, indent=2)
    sys.exit(0 if result.wasSuccessful() else 1)


if __name__ == "__main__":
    main()
