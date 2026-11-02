#!/usr/bin/env python3
"""Unit tests for surface_shader.py. Run with: python3 tools/test_surface_shader.py [--json report.json]

Reads etc/shaders/include/surface.wgsl on purpose: it checks that the committed shader, which the
engine loads, is what the generator writes from src/geometry/surface_math.c.
"""
import datetime as dt
import difflib
import json
import os
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import surface_shader

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMMITTED = os.path.join(ROOT, "etc", "shaders", "include", "surface.wgsl")


class SurfaceShaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.TemporaryDirectory()
        cls.path = os.path.join(cls.dir.name, "surface.wgsl")
        surface_shader.main([cls.path])
        with open(cls.path) as f:
            cls.generated = f.read()

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def test_matches_committed_shader(self):
        with open(COMMITTED) as f:
            committed = f.read()
        if self.generated != committed:
            diff = "".join(difflib.unified_diff(
                committed.splitlines(True), self.generated.splitlines(True),
                "etc/shaders/include/surface.wgsl", "tools/surface_shader.py"))
            self.fail("surface.wgsl differs from the generator output, run "
                      "python3 tools/surface_shader.py:\n" + diff)

    def test_global_surface_data(self):
        self.assertIn("var<private> surface_data: SurfaceData;", self.generated)
        self.assertNotIn("d: SurfaceData", self.generated)
        self.assertNotIn("d->", self.generated)
        self.assertNotIn("(d,", self.generated)

    def test_no_c_left(self):
        for text in ("/**", "box_sv", "box_sweep_sample", "box_surface", "float ", "int ",
                     "bool ", "found", "0f", "sqrtf", "fminf", "fmaxf", "(float)", "(int)"):
            self.assertNotIn(text, self.generated)

    def test_curve(self):
        self.assertIn('#include "curve.wgsl"', self.generated)
        self.assertIn("curve_cubic3(", self.generated)

    def test_declarations(self):
        self.assertIn("struct SurfaceSweepSample {", self.generated)
        self.assertIn("var result: SurfaceSweepSample;", self.generated)

    def test_early_returns(self):
        self.assertIn("    if (length < 0.000000000001) {\n        return sv(0.0, 1.0, 0.0);\n"
                      "    }\n\n    return sv_mul(a, 1.0 / length);\n", self.generated)
        self.assertNotIn("result = ", self.generated)

    def test_early_returns_rewrite(self):
        source = (
            "static box_sv sv(\n    float x)\n{\n    return (box_sv){x, x, x};\n}\n\n"
            "static float pick(\n    const box_surface_data_t *d,\n    int count)\n{\n"
            "    float result = 0.0f;\n"
            "    bool found = false;\n"
            "    for (int i = 0; !found && i < count; i ++) {\n"
            "        if (sf(d, i, 0) > 1.0f) {\n"
            "            result = (float)i;\n"
            "            found = true;\n"
            "        }\n"
            "    }\n\n"
            "    if (!found) {\n"
            "        result = 2.0f;\n"
            "    }\n\n"
            "    return result;\n}\n\n"
            "void box_surface_sample(void)\n{\n}\n")
        path = os.path.join(self.dir.name, "source.c")
        with open(path, "w") as f:
            f.write(source)
        self.assertIn(
            "fn pick(count: i32) -> f32 {\n"
            "    for (var i = 0; i < count; i ++) {\n"
            "        if (sf(i, 0) > 1.0) {\n"
            "            return f32(i);\n"
            "        }\n"
            "    }\n\n"
            "    return 2.0;\n"
            "}\n", surface_shader.generate(path))


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
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(SurfaceShaderTests)
    result = runner.run(suite)
    if json_path:
        records = sorted(getattr(result, "records", []), key=lambda r: r["case"])
        report = {
            "project": "surface_shader",
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
