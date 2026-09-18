#!/usr/bin/env python3
"""Unit tests for pm.py. Run with: python3 project/test_pm.py [--json report.json]"""
import datetime as dt
import json
import os
import shutil
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pm


class Args:
    def __init__(self, **kw):
        self.__dict__.update(kw)


class PmTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.old_db = pm.DB
        pm.DB = os.path.join(self.tmp, "db")
        os.makedirs(pm.DB)

    def tearDown(self):
        pm.DB = self.old_db
        shutil.rmtree(self.tmp, ignore_errors=True)

    def new(self, kind, path, title, **kw):
        args = Args(kind=kind, path=path, title=title, brief=kw.get("brief", "Brief."), type=kw.get("type", "feature"), deps=kw.get("deps"))
        pm.cmd_new(args)

    def test_new_creates_tree(self):
        self.new("epic", "e1", "Epic one")
        self.new("task", "e1/t1", "Task one", deps="e1/t0")
        self.new("subtask", "e1/t1/s1", "Sub one")
        self.assertTrue(os.path.isdir(os.path.join(pm.DB, "e1", "deliverable")))
        self.assertTrue(os.path.isdir(os.path.join(pm.DB, "e1", "t1", "deliverable")))
        self.assertFalse(os.path.isdir(os.path.join(pm.DB, "e1", "t1", "s1", "deliverable")))
        state = pm.collect_state()
        self.assertEqual(len(state["epics"]), 1)
        epic = state["epics"][0]
        self.assertEqual(epic["readme"]["title"], "Epic one")
        self.assertEqual(epic["readme"]["brief"], "Brief.")
        task = epic["children"][0]
        self.assertEqual(task["path"], "e1/t1")
        self.assertEqual(task["dependencies"], ["e1/t0"])
        self.assertEqual(task["children"][0]["level"], "subtask")

    def test_new_rejects_wrong_depth_and_missing_parent(self):
        with self.assertRaises(SystemExit):
            self.new("task", "e1", "Wrong")
        with self.assertRaises(SystemExit):
            self.new("task", "nope/t1", "Orphan")

    def test_status_transitions_set_times(self):
        self.new("epic", "e1", "Epic")
        pm.set_status("e1", "in progress")
        p = pm.read_json(os.path.join(pm.DB, "e1", "progress.json"))
        self.assertEqual(p["status"], "in progress")
        self.assertIsNotNone(p["start_time"])
        self.assertIsNone(p["end_time"])
        start = p["start_time"]
        pm.set_status("e1", "complete")
        p = pm.read_json(os.path.join(pm.DB, "e1", "progress.json"))
        self.assertEqual(p["start_time"], start)
        self.assertIsNotNone(p["end_time"])
        pm.set_status("e1", "pending")
        p = pm.read_json(os.path.join(pm.DB, "e1", "progress.json"))
        self.assertIsNone(p["start_time"])

    def test_deliverables_summarize_test_reports(self):
        self.new("epic", "e1", "Epic")
        self.new("task", "e1/t1", "Task")
        report = {"project": "core", "pass": 3, "fail": 1, "empty": 0, "elapsed": 1.5, "timestamp": "x",
                  "tests": [{"suite": "A", "case": "b", "status": "pass", "elapsed": 0.1}]}
        with open(os.path.join(pm.DB, "e1", "t1", "deliverable", "tests.json"), "w") as f:
            json.dump(report, f)
        with open(os.path.join(pm.DB, "e1", "t1", "deliverable", "shot.png"), "wb") as f:
            f.write(b"png")
        task = pm.collect_state()["epics"][0]["children"][0]
        names = {d["name"]: d for d in task["deliverables"]}
        self.assertEqual(names["tests.json"]["tests"]["pass"], 3)
        self.assertEqual(names["tests.json"]["tests"]["count"], 1)
        self.assertIsNone(names["shot.png"]["tests"])
        self.assertEqual(names["shot.png"]["url"], "/deliverable/e1/t1/deliverable/shot.png")

    def test_check_reports_problems(self):
        self.new("epic", "e1", "Epic")
        self.new("task", "e1/t1", "Task", deps="e1/missing")
        pm.set_status("e1/t1", "complete")
        with open(os.path.join(pm.DB, "e1", "t1", "README.md"), "w") as f:
            f.write("no title here\n")
        import io, contextlib
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = pm.cmd_check(None)
        self.assertEqual(rc, 1)
        text = out.getvalue()
        self.assertIn("has no title", text)
        self.assertIn("unknown dependency", text)
        self.assertIn("complete without a test result deliverable", text)

    def write_report(self, path, data, mtime=None):
        full = os.path.join(pm.DB, *path.split("/"))
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "w") as f:
            json.dump(data, f)
        if mtime is not None:
            os.utime(full, (mtime, mtime))
        return full

    def test_aggregate_tests_per_project_and_suite(self):
        self.new("epic", "e1", "Epic")
        self.new("task", "e1/t1", "Task")
        self.write_report("e1/t1/deliverable/tests.json", {
            "project": "core", "timestamp": "2026-01-01T10:00:00Z", "pass": 2, "fail": 1, "empty": 0, "elapsed": 0.5,
            "tests": [
                {"suite": "A", "case": "one", "status": "pass", "elapsed": 0.1},
                {"suite": "A", "case": "two", "status": "fail", "elapsed": 0.2},
                {"suite": "B", "case": "three", "status": "pass", "elapsed": 0.4},
            ],
        })
        self.write_report("e1/deliverable/tests.json", {
            "project": "core", "timestamp": "2026-01-02T10:00:00Z", "pass": 2, "fail": 0, "empty": 1, "elapsed": 0.7,
            "tests": [
                {"suite": "A", "case": "one", "status": "pass", "elapsed": 0.3},
                {"suite": "A", "case": "two", "status": "pass", "elapsed": 0.3},
                {"suite": "B", "case": "three", "status": "empty", "elapsed": 0.0},
            ],
        })
        agg = pm.aggregate_tests()
        self.assertEqual([r["time"] for r in agg["reports"]], ["2026-01-01T10:00:00Z", "2026-01-02T10:00:00Z"])
        first = agg["reports"][0]
        self.assertEqual((first["pass"], first["fail"], first["cases"]), (2, 1, 3))
        self.assertEqual(first["item"], "e1/t1")
        self.assertEqual([s["suite"] for s in first["suites"]], ["A", "B"])
        self.assertNotIn("case_list", first["suites"][0])

        suites = {s["suite"]: s for s in agg["suites"]}
        self.assertEqual(sorted(suites), ["A", "B"])
        self.assertEqual(suites["A"]["report_time"], "2026-01-02T10:00:00Z")
        self.assertEqual((suites["A"]["cases"], suites["A"]["pass"], suites["A"]["fail"]), (2, 2, 0))
        self.assertAlmostEqual(suites["A"]["time"], 0.6)
        self.assertEqual((suites["B"]["empty"], suites["B"]["pass"]), (1, 0))
        self.assertEqual([c["case"] for c in suites["A"]["cases_detail"]], ["one", "two"])
        self.assertEqual(suites["A"]["url"], "/deliverable/e1/deliverable/tests.json")

        self.assertEqual(len(agg["projects"]), 1)
        project = agg["projects"][0]
        self.assertEqual((project["project"], project["suites"], project["cases"]), ("core", 2, 3))
        self.assertEqual((project["pass"], project["fail"], project["empty"]), (2, 0, 1))

    def test_aggregate_tests_falls_back_to_mtime(self):
        self.new("epic", "e1", "Epic")
        self.write_report("e1/deliverable/tests.json", {
            "project": "core", "tests": [{"suite": "A", "case": "one", "status": "pass", "elapsed": 0.1}],
        }, mtime=1767261600)
        self.write_report("e1/deliverable/notes.json", {"note": "not a report"})
        agg = pm.aggregate_tests()
        self.assertEqual(len(agg["reports"]), 1)
        report = agg["reports"][0]
        self.assertEqual(report["time_source"], "mtime")
        self.assertEqual(report["time"], "2026-01-01T10:00:00Z")
        self.assertEqual((report["pass"], report["fail"], report["cases"]), (1, 0, 1))

    def test_aggregate_benchmarks_latest_per_scene(self):
        self.new("epic", "e1", "Epic")
        self.new("task", "e1/t1", "Task")
        self.write_report("e1/t1/deliverable/static_100k.json", {
            "scene": "../../test/integration/bench/static_100k.flecs", "entities": 110000, "frames": 300,
            "mean_ms": 2.0, "p50_ms": 1.9, "p99_ms": 4.0, "max_ms": 4.5, "fps": 500.0,
        }, mtime=1767261600)
        self.write_report("e1/deliverable/static_100k.json", {
            "scene": "../../test/integration/bench/static_100k.flecs", "entities": 110000, "frames": 300,
            "timestamp": "2026-01-02T10:00:00Z", "mean_ms": 1.25, "p50_ms": 1.22, "p99_ms": 2.6, "max_ms": 2.7, "fps": 800.0,
        })
        extra = os.path.join(self.tmp, "out")
        os.makedirs(extra)
        with open(os.path.join(extra, "bench.json"), "w") as f:
            json.dump({"kind": "bench", "results": [
                {"scene": "test/integration/bench/material_100k.flecs", "status": "pass", "timestamp": "2026-01-03T10:00:00Z",
                 "entities": 90000, "mean_ms": 1.1, "p99_ms": 2.2, "fps": 900.0},
            ]}, f)
        with open(os.path.join(extra, "visual.json"), "w") as f:
            json.dump({"kind": "visual", "results": [{"scene": "a.flecs", "status": "pass", "image": "a.png"}]}, f)
        agg = pm.aggregate_benchmarks(extra_dirs=[extra])
        self.assertEqual([r["scene"] for r in agg["runs"]], ["static_100k", "static_100k", "material_100k"])
        self.assertEqual([s["scene"] for s in agg["scenes"]], ["material_100k", "static_100k"])
        scenes = {s["scene"]: s for s in agg["scenes"]}
        self.assertEqual(scenes["static_100k"]["mean_ms"], 1.25)
        self.assertEqual(scenes["static_100k"]["time"], "2026-01-02T10:00:00Z")
        self.assertEqual(scenes["static_100k"]["item"], "e1")
        self.assertEqual(scenes["static_100k"]["url"], "/deliverable/e1/deliverable/static_100k.json")
        self.assertEqual(agg["runs"][0]["time_source"], "mtime")
        self.assertEqual(agg["runs"][0]["time"], "2026-01-01T10:00:00Z")
        self.assertEqual(scenes["material_100k"]["origin"], "integration")
        self.assertIsNone(scenes["material_100k"]["url"])
        self.assertEqual(scenes["material_100k"]["p99_ms"], 2.2)

    def test_aggregate_handles_empty_db(self):
        self.assertEqual(pm.aggregate_tests()["reports"], [])
        self.assertEqual(pm.aggregate_benchmarks(extra_dirs=[])["scenes"], [])

    def test_check_passes_clean_tree(self):
        self.new("epic", "e1", "Epic")
        self.new("task", "e1/t1", "Task")
        import io, contextlib
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(pm.cmd_check(None), 0)


class JsonResult(unittest.TextTestResult):
    def startTest(self, test):
        self.started = time.monotonic()
        super().startTest(test)

    def record(self, test, status):
        self.records = getattr(self, "records", [])
        self.records.append({"suite": type(test).__name__, "case": test._testMethodName, "status": status,
                             "elapsed": time.monotonic() - self.started})

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
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(PmTests)
    result = runner.run(suite)
    if json_path:
        records = getattr(result, "records", [])
        report = {
            "project": "project_tool",
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
