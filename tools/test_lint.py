#!/usr/bin/env python3
"""Unit tests for lint.py. Run with: python3 tools/test_lint.py [--json report.json]"""
import datetime as dt
import json
import os
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lint

HEADER = "/**\n * @file a.c\n * @brief Sample.\n */\n\n"

CLEAN_TEST = HEADER + (
    "void Suite_case(void) {\n"
    "    int x = 1;\n"
    "    if (x) {\n"
    "        x = 2;\n"
    "    }\n"
    "\n"
    "    x = 3;\n"
    "\n\n"
    "    return;\n"
    "}\n"
)

CLEAN_SRC = HEADER + (
    "/**\n"
    " * Adds one.\n"
    " *\n"
    " * @param x The value.\n"
    " * @return The value plus one.\n"
    " */\n"
    "static int add(\n"
    "    int x)\n"
    "{\n"
    "    int r = x + 1;\n"
    "\n"
    "    return r;\n"
    "}\n"
)

BAD_SRC = HEADER + (
    "/**\n"
    " * Adds.\n"
    " *\n"
    " * @param x The value.\n"
    " * @param y The other.\n"
    " * @return The sum.\n"
    " */\n"
    "static int add(int x, int y) {\n"
    "    int r = x + y;\n"
    "    if (r) {\n"
    "        r++;\n"
    "    }\n"
    "    r++;\n"
    "    return r;\n"
    "}\n"
)

BAD_TEST = HEADER + (
    "void S_c(void) {\n"
    "    int x = 1;\n"
    "    if (x) {\n"
    "        x = 2;\n"
    "    }\n"
    "    x = 3;\n"
    "}\n"
)


class LintTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)

    def write(self, text):
        path = os.path.join(self.dir.name, "a.c")
        with open(path, "w") as f:
            f.write(text)
        return path

    def read(self, path):
        with open(path) as f:
            return f.read()

    def check(self, rel, text):
        return lint.Lint(rel, text.splitlines(True)).run()

    def test_fix_leaves_clean_test_file_unchanged(self):
        self.assertEqual(self.check("test/core/a.c", CLEAN_TEST), [])
        path = self.write(CLEAN_TEST)
        lint.fix_file(path, "test/core/a.c")
        self.assertEqual(self.read(path), CLEAN_TEST)

    def test_fix_leaves_clean_source_file_unchanged(self):
        self.assertEqual(self.check("src/a.c", CLEAN_SRC), [])
        path = self.write(CLEAN_SRC)
        lint.fix_file(path, "src/a.c")
        self.assertEqual(self.read(path), CLEAN_SRC)

    def test_fix_leaves_file_without_trailing_newline_unchanged(self):
        text = CLEAN_SRC.rstrip("\n")
        path = self.write(text)
        lint.fix_file(path, "src/a.c")
        self.assertEqual(self.read(path), text)

    def test_fix_repairs_source_file(self):
        path = self.write(BAD_SRC)
        self.assertTrue(self.check("src/a.c", BAD_SRC))
        lint.fix_file(path, "src/a.c")
        fixed = self.read(path)
        self.assertEqual(self.check("src/a.c", fixed), [])
        self.assertIn("static int add(\n    int x,\n    int y)\n{\n", fixed)

    def test_fix_is_idempotent(self):
        path = self.write(BAD_SRC)
        lint.fix_file(path, "src/a.c")
        once = self.read(path)
        lint.fix_file(path, "src/a.c")
        self.assertEqual(self.read(path), once)

    def test_fix_repairs_blank_lines_in_test_body_only(self):
        path = self.write(BAD_TEST)
        self.assertTrue(self.check("test/core/a.c", BAD_TEST))
        lint.fix_file(path, "test/core/a.c")
        fixed = self.read(path)
        self.assertEqual(self.check("test/core/a.c", fixed), [])
        self.assertIn("    }\n\n    x = 3;\n", fixed)
        self.assertIn("void S_c(void) {\n", fixed)


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
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(LintTests)
    result = runner.run(suite)
    if json_path:
        records = sorted(getattr(result, "records", []), key=lambda r: r["case"])
        report = {
            "project": "lint",
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
