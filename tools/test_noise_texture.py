#!/usr/bin/env python3
"""Unit tests for noise_texture.py. Run with: python3 tools/test_noise_texture.py [--json report.json]"""
import datetime as dt
import json
import os
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import noise_texture

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEST_TEXTURE = os.path.join(ROOT, "test", "assets", "textures", "cloud_noise_256.png")


def max_step(a, b):
    """Largest absolute difference between the texels of two equally long rows."""
    return max(abs(x - y) for x, y in zip(a, b))


class NoiseTextureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.TemporaryDirectory()
        cls.path = os.path.join(cls.dir.name, "noise.png")
        noise_texture.generate(cls.path, size=128, frequency=4, octaves=5, seed=7)
        cls.rows = noise_texture.read_png(cls.path)

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def test_size(self):
        self.assertEqual(len(self.rows), 128)
        self.assertTrue(all(len(row) == 128 for row in self.rows))

    def test_left_right_wrap(self):
        columns = list(zip(*self.rows))
        inside = max(max_step(columns[x], columns[x + 1]) for x in range(len(columns) - 1))
        self.assertLessEqual(max_step(columns[-1], columns[0]), inside)

    def test_top_bottom_wrap(self):
        rows = self.rows
        inside = max(max_step(rows[y], rows[y + 1]) for y in range(len(rows) - 1))
        self.assertLessEqual(max_step(rows[-1], rows[0]), inside)

    def test_wrap_mean_step(self):
        """The mean difference across the wraps is like the one between neighbours inside."""
        rows = self.rows
        n = len(rows)

        def mean_step(a, b):
            return sum(abs(x - y) for x, y in zip(a, b)) / len(a)

        inside = sum(mean_step(rows[y], rows[y + 1]) for y in range(n - 1)) / (n - 1)
        self.assertLess(mean_step(rows[-1], rows[0]), 2.0 * inside + 1.0)
        columns = list(zip(*rows))
        inside = sum(mean_step(columns[x], columns[x + 1]) for x in range(n - 1)) / (n - 1)
        self.assertLess(mean_step(columns[-1], columns[0]), 2.0 * inside + 1.0)

    def test_deterministic(self):
        again = os.path.join(self.dir.name, "again.png")
        noise_texture.generate(again, size=128, frequency=4, octaves=5, seed=7)
        with open(self.path, "rb") as a, open(again, "rb") as b:
            self.assertEqual(a.read(), b.read())

    def test_seed_changes_output(self):
        other = os.path.join(self.dir.name, "other.png")
        noise_texture.generate(other, size=128, frequency=4, octaves=5, seed=8)
        self.assertNotEqual(noise_texture.read_png(other), self.rows)

    def test_histogram_centered(self):
        texels = sorted(b for row in self.rows for b in row)
        n = len(texels)
        self.assertLessEqual(abs(texels[n // 2] - 127.5), 1.0)
        self.assertLessEqual(abs(sum(texels) / n - 127.5), 8.0)
        low = sum(1 for b in texels if b == 0) / n
        high = sum(1 for b in texels if b == 255) / n
        self.assertGreater(low, 0.002)
        self.assertLess(low, 0.02)
        self.assertGreater(high, 0.002)
        self.assertLess(high, 0.02)
        # Spans most of 0..255: the 5th and 95th percentiles are well apart.
        self.assertLess(texels[int(0.05 * n)], 80)
        self.assertGreater(texels[int(0.95 * n)], 175)

    def test_threshold_half_covers_half(self):
        texels = [b for row in self.rows for b in row]
        below = sum(1 for b in texels if b < 128) / len(texels)
        self.assertAlmostEqual(below, 0.5, delta=0.02)

    def test_png_roundtrip(self):
        rows = [bytes((x * 7 + y * 13) & 0xFF for x in range(9)) for y in range(5)]
        path = os.path.join(self.dir.name, "roundtrip.png")
        noise_texture.write_png(path, rows)
        self.assertEqual(noise_texture.read_png(path), rows)

    def test_main_writes_file(self):
        path = os.path.join(self.dir.name, "main.png")
        noise_texture.main([path, "--size", "32", "--frequency", "2", "--octaves", "3",
                            "--seed", "3"])
        rows = noise_texture.read_png(path)
        self.assertEqual((len(rows), len(rows[0])), (32, 32))

    def test_test_asset_matches_tool(self):
        """test/assets/textures/cloud_noise_256.png is what the command in the header writes."""
        path = os.path.join(self.dir.name, "cloud_noise_256.png")
        noise_texture.generate(path, size=256)
        self.assertEqual(noise_texture.read_png(path), noise_texture.read_png(TEST_TEXTURE))


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
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(NoiseTextureTests)
    result = runner.run(suite)
    if json_path:
        records = sorted(getattr(result, "records", []), key=lambda r: r["case"])
        report = {
            "project": "noise_texture",
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
