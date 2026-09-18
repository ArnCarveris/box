#!/usr/bin/env python3
"""Unit tests for pm.py. Run with: python3 project/test_pm.py [--json report.json]"""
import datetime as dt
import http.server
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pm


PLAN_STUBS = """
globalThis.esc = s => String(s ?? "").replace(/[&<>"]/g, c => ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"}[c]));
globalThis.element = { innerHTML: "", querySelector: () => null };
globalThis.document = { getElementById: () => element, querySelector: () => element };
globalThis.localStorage = { getItem: () => null, setItem: () => {} };
const task = (path, title, status, deps, subs) => ({
  path, name: path.split("/")[1], level: "task", status, dependencies: deps,
  readme: { title }, children: (subs || []).map((d, i) => ({
    path: path + "/s" + i, name: "s" + i, level: "subtask", status: "pending",
    dependencies: d, readme: { title: "Sub " + i }, children: [],
  })),
});
globalThis.EPICS = [
  { name: "core", priority: 10, created: "2026-01-01", status: "complete", readme: { title: "Core" }, children: [
    task("core/boot", "Boot", "complete", []),
    task("core/loop", "Main loop", "complete", ["core/boot"]),
  ] },
  { name: "render", priority: 20, created: "2026-01-02", status: "in progress", readme: { title: "Render" }, children: [
    task("render/pipeline", "Pipeline", "in progress", ["core"]),
    task("render/shadows", "Shadows", "pending", ["render/pipeline"]),
    task("render/post", "Post effects", "complete", ["render/pipeline"]),
  ] },
  { name: "ui", priority: 5, created: "2026-01-03", status: "pending", readme: { title: "Interface" }, children: [
    task("ui/panel", "Panels", "pending", []),
    task("ui/theme", "Theming", "pending", ["ui/panel"], [["render/shadows", "ui/theme"]]),
  ] },
];
globalThis.overlaps = list => {
  const hits = [];
  for (let i = 0; i < list.length; i++) {
    for (let j = i + 1; j < list.length; j++) {
      const a = list[i], b = list[j];
      if (a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h) hits.push([a.name, b.name]);
    }
  }
  return hits;
};
globalThis.count = (text, needle) => text.split(needle).length - 1;
"""


class Args:
    def __init__(self, **kw):
        self.__dict__.update(kw)


class PmTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.old_db = pm.DB
        self.old_bench = pm.BENCH_PROJECTS
        self.old_inspector = pm.INSPECTOR
        pm.DB = os.path.join(self.tmp, "db")
        pm.BENCH_PROJECTS = os.path.join(self.tmp, "bench")
        pm.INSPECTOR = os.path.join(self.tmp, "inspector", "index.html")
        os.makedirs(pm.DB)

    def tearDown(self):
        pm.DB = self.old_db
        pm.BENCH_PROJECTS = self.old_bench
        pm.INSPECTOR = self.old_inspector
        shutil.rmtree(self.tmp, ignore_errors=True)

    def get(self, route, body=None):
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), pm.Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        url = "http://127.0.0.1:%d%s" % (server.server_address[1], route)
        request = urllib.request.Request(url, data=body, headers={"Content-Type": "application/json"} if body else {})
        try:
            with urllib.request.urlopen(request) as response:
                return response.status, response.headers.get("Content-Type"), response.read()
        except urllib.error.HTTPError as err:
            return err.code, err.headers.get("Content-Type"), err.read()
        finally:
            server.shutdown()
            server.server_close()
            thread.join()

    def post_priority(self, payload):
        status, _, body = self.get("/api/priority", json.dumps(payload).encode())
        return status, json.loads(body)

    def progress(self, path, **fields):
        full = os.path.join(pm.DB, *path.split("/"), "progress.json")
        with open(full) as f:
            data = json.load(f)
        for key, value in fields.items():
            if value is None:
                data.pop(key, None)
            else:
                data[key] = value
        with open(full, "w") as f:
            json.dump(data, f, indent=2)

    def new(self, kind, path, title, **kw):
        args = Args(kind=kind, path=path, title=title, brief=kw.get("brief", "Brief."), type=kw.get("type", "feature"),
                    deps=kw.get("deps"), priority=kw.get("priority"))
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

    def test_aggregate_tests_builds_suite_and_case_history(self):
        self.new("epic", "e1", "Epic")
        self.new("task", "e1/t1", "Task")
        self.write_report("e1/t1/deliverable/tests.json", {
            "project": "core", "timestamp": "2026-01-01T10:00:00Z", "elapsed": 0.5,
            "tests": [
                {"suite": "A", "case": "one", "status": "pass", "elapsed": 0.1},
                {"suite": "A", "case": "two", "status": "fail", "elapsed": 0.2},
            ],
        })
        self.write_report("e1/deliverable/tests.json", {
            "project": "core", "timestamp": "2026-01-02T10:00:00Z", "elapsed": 0.7,
            "tests": [
                {"suite": "A", "case": "one", "status": "pass", "elapsed": 0.3},
                {"suite": "A", "case": "two", "status": "pass", "elapsed": 0.4},
            ],
        })
        agg = pm.aggregate_tests()
        self.assertEqual(agg["suites"][0]["key"], "core|A")
        history = agg["suite_history"]["core|A"]
        self.assertEqual([p["time"] for p in history], ["2026-01-01T10:00:00Z", "2026-01-02T10:00:00Z"])
        self.assertEqual([p["item"] for p in history], ["e1/t1", "e1"])
        self.assertAlmostEqual(history[0]["elapsed"], 0.3)
        self.assertAlmostEqual(history[1]["elapsed"], 0.7)
        self.assertEqual((history[0]["pass"], history[0]["fail"], history[0]["cases"]), (1, 1, 2))
        self.assertEqual(history[1]["url"], "/deliverable/e1/deliverable/tests.json")
        cases = agg["case_history"]["core|A|two"]
        self.assertEqual([p["status"] for p in cases], ["fail", "pass"])
        self.assertAlmostEqual(cases[1]["elapsed"], 0.4)
        self.assertEqual(len(agg["case_history"]["core|A|one"]), 2)

    def test_aggregate_benchmarks_builds_micro_suite_history(self):
        self.new("epic", "e1", "Epic")
        self.new("task", "e1/t1", "Task")
        self.write_report("e1/t1/deliverable/bench.json", {
            "project": "bench_transform", "timestamp": "2026-01-01T10:00:00Z",
            "benchmarks": [
                {"suite": "Static", "case": "set_leaf", "median_ns": 200.0, "mean_ns": 201.0,
                 "ci_low_ns": 198.0, "ci_high_ns": 204.0, "p99_ns": 260.0, "outliers": 3, "samples": 100},
                {"suite": "Static", "case": "read_leaf", "median_ns": 100.0, "mean_ns": 101.0, "samples": 100},
                {"suite": "Dynamic", "case": "propagate", "median_ns": 9000.0, "mean_ns": 9100.0, "samples": 20},
            ],
        })
        self.write_report("e1/deliverable/bench.json", {
            "project": "bench_transform", "timestamp": "2026-01-02T10:00:00Z",
            "benchmarks": [
                {"suite": "Static", "case": "set_leaf", "median_ns": 180.0, "mean_ns": 181.0,
                 "ci_low_ns": 178.0, "ci_high_ns": 184.0, "p99_ns": 240.0, "outliers": 1, "samples": 100},
                {"suite": "Static", "case": "read_leaf", "median_ns": 90.0, "mean_ns": 91.0, "samples": 100},
            ],
        })
        agg = pm.aggregate_benchmarks(extra_dirs=[])
        self.assertEqual([p["project"] for p in agg["micro_projects"]], ["bench_transform"])
        project = agg["micro_projects"][0]
        self.assertEqual((project["suites"], project["cases"]), (2, 3))
        self.assertEqual(project["median_ns"], 9270.0)
        self.assertEqual([s["key"] for s in agg["micro_suites"]],
                         ["bench_transform|Dynamic", "bench_transform|Static"])
        static = [s for s in agg["micro_suites"] if s["suite"] == "Static"][0]
        self.assertEqual((static["cases"], static["median_ns"], static["time"]),
                         (2, 270.0, "2026-01-02T10:00:00Z"))
        self.assertEqual([c["case"] for c in static["cases_detail"]], ["read_leaf", "set_leaf"])
        self.assertEqual(static["cases_detail"][1]["p99_ns"], 240.0)
        self.assertEqual(static["cases_detail"][1]["outliers"], 1)
        history = agg["micro_suite_history"]["bench_transform|Static"]
        self.assertEqual([(p["time"], p["median_ns"], p["cases"]) for p in history],
                         [("2026-01-01T10:00:00Z", 300.0, 2), ("2026-01-02T10:00:00Z", 270.0, 2)])
        self.assertEqual(len(agg["micro_suite_history"]["bench_transform|Dynamic"]), 1)
        cases = agg["micro_case_history"]["bench_transform|Static|set_leaf"]
        self.assertEqual([(p["median_ns"], p["ci_low_ns"], p["ci_high_ns"]) for p in cases],
                         [(200.0, 198.0, 204.0), (180.0, 178.0, 184.0)])
        self.assertEqual(cases[1]["url"], "/deliverable/e1/deliverable/bench.json")

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

    def test_aggregate_benchmarks_reads_micro_reports(self):
        self.new("epic", "e1", "Epic")
        self.new("task", "e1/t1", "Task")
        self.write_report("e1/t1/deliverable/bench.json", {
            "project": "bench_transform", "timestamp": "2026-01-01T10:00:00Z",
            "benchmarks": [
                {"suite": "Static", "case": "set_leaf_position", "median_ns": 200.0, "mean_ns": 201.0,
                 "ci_low_ns": 198.0, "ci_high_ns": 204.0, "stddev_ns": 2.0, "iterations": 1000, "samples": 100},
                {"suite": "Dynamic", "case": "propagate_subtree", "median_ns": 18000.0, "mean_ns": 18100.0,
                 "ci_low_ns": 17900.0, "ci_high_ns": 18300.0, "stddev_ns": 50.0, "iterations": 50, "samples": 20},
            ],
        })
        self.write_report("e1/deliverable/transform.json", {
            "project": "bench_transform", "timestamp": "2026-01-02T10:00:00Z",
            "benchmarks": [
                {"suite": "Static", "case": "set_leaf_position", "median_ns": 180.0, "mean_ns": 181.0,
                 "ci_low_ns": 178.0, "ci_high_ns": 184.0, "samples": 100},
            ],
        })
        agg = pm.aggregate_benchmarks(extra_dirs=[])
        self.assertEqual(agg["scenes"], [])
        self.assertEqual([r["key"] for r in agg["micro"]],
                         ["bench_transform/Dynamic.propagate_subtree", "bench_transform/Static.set_leaf_position"])
        self.assertEqual(len(agg["micro_runs"]), 3)
        latest = {r["key"]: r for r in agg["micro"]}
        row = latest["bench_transform/Static.set_leaf_position"]
        self.assertEqual((row["median_ns"], row["mean_ns"], row["ci_low_ns"], row["ci_high_ns"]),
                         (180.0, 181.0, 178.0, 184.0))
        self.assertEqual(row["time"], "2026-01-02T10:00:00Z")
        self.assertEqual(row["item"], "e1")
        self.assertEqual(row["url"], "/deliverable/e1/deliverable/transform.json")
        self.assertEqual(latest["bench_transform/Dynamic.propagate_subtree"]["samples"], 20)
        self.assertEqual(latest["bench_transform/Dynamic.propagate_subtree"]["suite"], "Dynamic")

    def test_check_warns_for_completed_task_without_bench_report(self):
        config = os.path.join(pm.BENCH_PROJECTS, "transform", "project.json")
        os.makedirs(os.path.dirname(config))
        with open(config, "w") as f:
            f.write("{}")
        os.utime(config, (1767261600, 1767261600))
        self.new("epic", "e1", "Epic")
        self.new("task", "e1/t1", "Task")
        self.new("task", "e1/t2", "Other task")
        self.new("task", "e1/t3", "Page task")
        self.new("epic", "project_tool", "Tooling")
        self.new("task", "project_tool/t1", "Tool task")
        for path in ("e1/t1", "e1/t2", "e1/t3", "project_tool/t1"):
            pm.set_status(path, "complete")
            self.write_report(path + "/deliverable/tests.json", {"project": "core", "tests": []})
        self.write_report("e1/t2/deliverable/bench.json", {"project": "bench_transform", "benchmarks": []})
        progress_path = os.path.join(pm.DB, "e1", "t3", "progress.json")
        with open(progress_path) as f:
            progress = json.load(f)
        progress["bench"] = False
        with open(progress_path, "w") as f:
            json.dump(progress, f)
        import io, contextlib
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = pm.cmd_check(None)
        text = out.getvalue()
        self.assertEqual(rc, 0)
        self.assertIn("e1/t1: warning: complete without deliverable/bench.json", text)
        self.assertNotIn("e1/t2: warning", text)
        self.assertNotIn("e1/t3: warning", text)
        self.assertNotIn("project_tool/t1: warning", text)
        self.assertIn("1 warning(s)", text)

    def test_aggregate_handles_empty_db(self):
        self.assertEqual(pm.aggregate_tests()["reports"], [])
        self.assertEqual(pm.aggregate_benchmarks(extra_dirs=[])["scenes"], [])

    def test_check_passes_clean_tree(self):
        self.new("epic", "e1", "Epic")
        self.new("task", "e1/t1", "Task")
        import io, contextlib
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(pm.cmd_check(None), 0)


    def test_epic_priority_defaults_and_is_seeded(self):
        self.new("epic", "a", "Epic a")
        self.new("epic", "b", "Epic b", priority=30)
        self.new("task", "a/t1", "Task")
        self.assertEqual(pm.read_json(os.path.join(pm.DB, "b", "progress.json"))["priority"], 30)
        self.assertNotIn("priority", pm.read_json(os.path.join(pm.DB, "a", "t1", "progress.json")))
        self.progress("a", priority=None)
        epics = {e["name"]: e for e in pm.collect_state()["epics"]}
        self.assertEqual(epics["a"]["priority"], pm.PRIORITY_DEFAULT)
        self.assertEqual(epics["b"]["priority"], 30)
        self.assertNotIn("priority", epics["a"]["children"][0])
        self.progress("a", priority="soon")
        epics = {e["name"]: e for e in pm.collect_state()["epics"]}
        self.assertEqual(epics["a"]["priority"], pm.PRIORITY_DEFAULT)
        self.assertIsNone(pm.set_priority("a", 20))
        seeded = pm.read_json(os.path.join(pm.DB, "a", "progress.json"))
        self.assertEqual(seeded["priority"], 20)
        self.assertEqual(seeded["kind"], "feature")
        self.assertEqual(seeded["status"], "pending")
        self.assertEqual(pm.set_priority("gone", 10), "unknown epic 'gone'")

    def test_epics_and_tasks_sort_by_priority_and_derived_order(self):
        for name, priority in (("a", 30), ("b", 10), ("c", 20), ("d", 40)):
            self.new("epic", name, "Epic " + name, priority=priority)
        self.assertEqual([e["name"] for e in pm.collect_state()["epics"]], ["b", "c", "a", "d"])
        pm.set_status("d", "in progress")
        self.assertEqual([e["name"] for e in pm.collect_state()["epics"]], ["d", "b", "c", "a"])
        self.new("task", "b/t1", "One", deps="b/t2")
        self.new("task", "b/t2", "Two")
        self.progress("b/t1", created="2026-01-01T00:00:00Z")
        self.progress("b/t2", created="2026-01-02T00:00:00Z")
        epics = {e["name"]: e for e in pm.collect_state()["epics"]}
        self.assertEqual([t["name"] for t in epics["b"]["children"]], ["t2", "t1"])

    def test_derived_order_follows_dependencies_then_priority_then_creation(self):
        self.new("epic", "base", "Base", priority=1)
        self.new("epic", "fast", "Fast", priority=5)
        self.new("epic", "slow", "Slow", priority=50)
        self.new("epic", "late", "Late", priority=2)
        self.new("task", "base/t", "Base task")
        self.new("task", "fast/t1", "Fast one", deps="base/t")
        self.new("task", "fast/t2", "Fast two", deps="slow/t1")
        self.new("task", "slow/t1", "Slow one", deps="base/t")
        self.new("task", "slow/t2", "Slow two", deps="base/t")
        self.new("task", "late/t", "Late task", deps="slow")
        pm.set_status("base/t", "complete")
        self.progress("slow/t1", created="2026-01-02T00:00:00Z")
        self.progress("slow/t2", created="2026-01-01T00:00:00Z")
        state = pm.collect_state()
        plan = pm.derived_plan(state["epics"])
        self.assertEqual([i["path"] for i in plan["order"]],
                         ["fast/t1", "slow/t2", "slow/t1", "late/t", "fast/t2"])
        self.assertEqual(plan["blocked"], [])
        self.assertEqual([i["path"] for i in state["next_up"]], ["fast/t1", "slow/t2", "slow/t1"])
        first = state["next_up"][0]
        self.assertEqual((first["epic"], first["epic_title"], first["level"], first["title"]),
                         ("fast", "Fast", "task", "Fast one"))

    def test_derived_order_covers_subtasks_and_skips_started_tasks(self):
        self.new("epic", "a", "Epic a", priority=10)
        self.new("task", "a/t1", "Task one")
        self.new("subtask", "a/t1/s1", "Sub one")
        self.new("task", "a/t2", "Task two", deps="a/t1")
        pm.set_status("a/t1", "in progress")
        state = pm.collect_state()
        plan = pm.derived_plan(state["epics"])
        self.assertEqual([i["path"] for i in plan["order"]], ["a/t1", "a/t1/s1", "a/t2"])
        self.assertEqual([i["path"] for i in state["next_up"]], ["a/t1/s1"])
        self.assertEqual(state["next_up"][0]["level"], "subtask")

    def test_priority_post_rewrites_progress_and_the_state(self):
        self.new("epic", "a", "Epic a", priority=10)
        self.new("epic", "b", "Epic b", priority=20)
        status, data = self.post_priority({"epic": "b", "priority": 5})
        self.assertEqual((status, data), (200, {"epic": "b", "priority": 5}))
        progress = pm.read_json(os.path.join(pm.DB, "b", "progress.json"))
        self.assertEqual(progress["priority"], 5)
        self.assertEqual((progress["kind"], progress["status"], progress["dependencies"]), ("feature", "pending", []))
        self.assertIsNotNone(progress["created"])
        self.assertEqual([e["name"] for e in pm.collect_state()["epics"]], ["b", "a"])
        status, _, body = self.get("/api/state")
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(body)["epics"][0]["priority"], 5)

    def test_priority_post_rejects_unknown_epics_and_bad_values(self):
        self.new("epic", "a", "Epic a", priority=10)
        for payload, message in (({"epic": "gone", "priority": 1}, "unknown epic 'gone'"),
                                 ({"epic": "a/t1", "priority": 1}, "'a/t1' is not an epic name"),
                                 ({"epic": "a", "priority": "soon"}, "priority must be an integer"),
                                 ({"epic": "a", "priority": True}, "priority must be an integer"),
                                 ({"epic": "a", "priority": 1.5}, "priority must be an integer")):
            status, data = self.post_priority(payload)
            self.assertEqual((status, data["error"]), (400, message), payload)
        status, _, body = self.get("/api/priority", b"not json")
        self.assertEqual(status, 400)
        self.assertIn(b"json", body)
        status, data = self.post_priority([1, 2])
        self.assertEqual(status, 400)
        self.assertEqual(pm.read_json(os.path.join(pm.DB, "a", "progress.json"))["priority"], 10)
        self.assertEqual(self.get("/api/plan")[0], 404)

    def test_check_warns_when_an_epic_runs_before_its_dependency(self):
        self.new("epic", "a", "Epic a", priority=20)
        self.new("epic", "b", "Epic b", deps="a", priority=10)
        import io, contextlib
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = pm.cmd_check(None)
        text = out.getvalue()
        self.assertEqual(rc, 0)
        self.assertIn("b: warning: priority 10 runs before its dependency a (20)", text)
        self.assertIn("0 problem(s), 1 warning(s)", text)
        pm.set_priority("b", 30)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            pm.cmd_check(None)
        self.assertIn("0 problem(s), 0 warning(s)", out.getvalue())

    def test_check_reports_tasks_that_can_never_become_ready(self):
        self.new("epic", "a", "Epic a", priority=10)
        self.new("task", "a/t1", "One", deps="a/t2")
        self.new("task", "a/t2", "Two", deps="a/t1")
        self.new("task", "a/t3", "Three")
        import io, contextlib
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = pm.cmd_check(None)
        text = out.getvalue()
        self.assertEqual(rc, 1)
        self.assertIn("a/t1: dependency cycle, this task can never become ready", text)
        self.assertIn("a/t2: dependency cycle, this task can never become ready", text)
        self.assertNotIn("a/t3: dependency", text)
        self.assertEqual([i["path"] for i in pm.collect_state()["next_up"]], ["a/t3"])

    def plan_logic(self):
        with open(os.path.join(pm.ROOT, "index.html")) as f:
            text = f.read()
        start = text.index("// --- plan logic")
        end = text.index("// --- end plan logic")
        return text[start:end]

    def run_plan_js(self, script):
        if not shutil.which("node"):
            self.skipTest("node is not installed")
        path = os.path.join(self.tmp, "plan_check.js")
        with open(path, "w") as f:
            f.write(PLAN_STUBS + self.plan_logic() + script)
        run = subprocess.run([shutil.which("node"), path], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        return json.loads(run.stdout)

    def test_plan_page_lays_out_tasks_in_dependency_columns(self):
        out = self.run_plan_js("""
const layout = planLayout(EPICS, {});
element.innerHTML = planSvg(layout, { x: 0, y: 0, k: 1 });
const markup = element.innerHTML;
console.log(JSON.stringify({
  depth: layout.depth,
  columns: layout.list.map(n => [n.name, n.col]),
  rows: layout.list.map(n => [n.name, n.row]),
  xs: layout.list.map(n => [n.col, n.x]),
  bands: layout.bands.map(b => [b.epic, b.y, b.height, b.tasks]),
  overlaps: overlaps(layout.list),
  size: [layout.width > 0, layout.height > 0],
  nodes: count(markup, "data-node="),
  edges: count(markup, 'class="plan-dep'),
  cross: count(markup, "plan-dep cross"),
  bandLabels: count(markup, 'class="plan-band"'),
  complete: count(markup, "plan-node complete"),
  inProgress: count(markup, "plan-node in-progress"),
  svg: markup.startsWith("<svg") && markup.includes('id="plan-zoom"'),
  epicTag: markup.includes("Render \u00b7 in progress"),
  related: planRelated(layout, "render/pipeline"),
  handles: count(markup, "data-handle="),
}));
""")
        self.assertEqual(out["depth"], {"ui/panel": 0, "ui/theme": 4, "core/boot": 0, "core/loop": 1,
                                        "render/pipeline": 2, "render/shadows": 3, "render/post": 3})
        for name, column in out["columns"]:
            self.assertEqual(column, out["depth"][name])
        for (col_a, x_a), (col_b, x_b) in zip(sorted(out["xs"]), sorted(out["xs"])[1:]):
            self.assertLessEqual(x_a, x_b)
            self.assertEqual(col_a < col_b, x_a < x_b)
        self.assertEqual([b[0] for b in out["bands"]], ["ui", "core", "render"])
        self.assertEqual([b[3] for b in out["bands"]], [2, 2, 3])
        for (_, y_a, h_a, _), (_, y_b, _, _) in zip(out["bands"], out["bands"][1:]):
            self.assertGreater(y_b, y_a + h_a)
        self.assertEqual(dict(out["rows"])["render/post"], 1)
        self.assertEqual(dict(out["rows"])["render/shadows"], 0)
        self.assertEqual(out["overlaps"], [])
        self.assertEqual(out["size"], [True, True])
        self.assertEqual((out["nodes"], out["edges"], out["cross"]), (7, 6, 2))
        self.assertEqual((out["bandLabels"], out["complete"], out["inProgress"]), (3, 3, 1))
        self.assertEqual(out["handles"], 0)
        self.assertTrue(out["svg"])
        self.assertTrue(out["epicTag"])
        self.assertEqual(sorted(out["related"]["nodes"]),
                         ["core/loop", "render/pipeline", "render/post", "render/shadows"])
        self.assertEqual(sorted(out["related"]["edges"]),
                         ["core/loop > render/pipeline", "render/pipeline > render/post",
                          "render/pipeline > render/shadows"])

    def test_plan_page_filters_by_epic_and_unfinished_tasks(self):
        out = self.run_plan_js("""
const one = planLayout(EPICS, { epic: "render" });
const open = planLayout(EPICS, { open: true });
console.log(JSON.stringify({
  order: planEpics(EPICS).map(e => e.name),
  one: one.list.map(n => n.name),
  oneDepth: one.depth,
  oneEdges: one.edges.length,
  oneBands: one.bands.map(b => b.epic),
  open: open.list.map(n => n.name),
  openBands: open.bands.map(b => b.epic),
  openCross: open.edges.filter(e => e.cross).length,
  openDepth: open.depth,
  none: planLayout(EPICS, { epic: "nope" }).list.length,
  subDeps: (planLayout(EPICS, {}).into["ui/theme"] || []).slice().sort(),
}));
""")
        self.assertEqual(out["order"], ["ui", "core", "render"])
        self.assertEqual(out["one"], ["render/pipeline", "render/shadows", "render/post"])
        self.assertEqual(out["oneDepth"], {"render/pipeline": 0, "render/shadows": 1, "render/post": 1})
        self.assertEqual((out["oneEdges"], out["oneBands"]), (2, ["render"]))
        self.assertEqual(out["open"], ["ui/panel", "ui/theme", "render/pipeline", "render/shadows"])
        self.assertEqual(out["openBands"], ["ui", "render"])
        self.assertEqual((out["openCross"], out["openDepth"]["render/pipeline"]), (1, 0))
        self.assertEqual(out["none"], 0)
        self.assertEqual(out["subDeps"], ["render/shadows", "ui/panel"])

    def write_wasm(self, epic):
        directory = os.path.join(pm.DB, epic, "deliverable", "wasm")
        os.makedirs(directory, exist_ok=True)
        files = (("box_app.js", b"var Module;"), ("box_app.wasm", b"\0asm\1\0\0\0"),
                 ("box_app.html", b"<html></html>"))
        for name, body in files:
            with open(os.path.join(directory, name), "wb") as f:
                f.write(body)

    def test_state_links_epics_with_a_wasm_deliverable(self):
        self.new("epic", "e1", "Epic one")
        self.new("task", "e1/t1", "Task")
        self.new("epic", "e2", "Epic two")
        self.write_wasm("e1")
        epics = {e["name"]: e for e in pm.collect_state()["epics"]}
        self.assertEqual(epics["e1"]["wasm_url"], "/deliverable/e1/deliverable/wasm/box_app.js")
        self.assertEqual(epics["e1"]["inspect_url"],
                         "/inspector?wasm=/deliverable/e1/deliverable/wasm/box_app.js"
                         "&scene=etc/scenes/primitives.flecs")
        self.assertIsNone(epics["e2"]["wasm_url"])
        self.assertIsNone(epics["e2"]["inspect_url"])
        self.assertNotIn("inspect_url", epics["e1"]["children"][0])

    def test_content_type_per_extension(self):
        self.assertEqual(pm.content_type("/a/box_app.wasm"), "application/wasm")
        self.assertEqual(pm.content_type("/a/box_app.js"), "application/javascript")
        self.assertEqual(pm.content_type("/a/box_app.HTML"), "text/html")
        self.assertEqual(pm.content_type("/a/shot.png"), "image/png")
        self.assertEqual(pm.content_type("/a/tests.json"), "application/json")
        self.assertEqual(pm.content_type("/a/session.log"), "text/plain")
        self.assertEqual(pm.content_type("/a/README.md"), "text/plain")
        self.assertEqual(pm.content_type("/a/noext"), "text/plain")

    def test_deliverable_route_serves_wasm_with_its_content_type(self):
        self.new("epic", "e1", "Epic")
        self.write_wasm("e1")
        status, ctype, body = self.get("/deliverable/e1/deliverable/wasm/box_app.wasm")
        self.assertEqual((status, ctype), (200, "application/wasm"))
        self.assertEqual(body, b"\0asm\1\0\0\0")
        status, ctype, _ = self.get("/deliverable/e1/deliverable/wasm/box_app.js")
        self.assertEqual((status, ctype), (200, "application/javascript"))
        self.assertEqual(self.get("/deliverable/e1/deliverable/wasm/missing.js")[0], 404)

    def test_inspector_route_reads_the_page_from_disk(self):
        os.makedirs(os.path.dirname(pm.INSPECTOR))
        with open(pm.INSPECTOR, "w") as f:
            f.write("<html>one</html>")
        status, ctype, body = self.get("/inspector")
        self.assertEqual((status, ctype), (200, "text/html; charset=utf-8"))
        self.assertEqual(body, b"<html>one</html>")
        with open(pm.INSPECTOR, "w") as f:
            f.write("<html>two</html>")
        self.assertEqual(self.get("/inspector?wasm=/deliverable/e1/deliverable/wasm/box_app.js")[2],
                         b"<html>two</html>")

    def test_inspector_route_404_when_the_page_is_missing(self):
        status, _, body = self.get("/inspector")
        self.assertEqual(status, 404)
        self.assertIn(b"box_inspector", body)


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
