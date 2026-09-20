#!/usr/bin/env python3
"""Project management tool for box: walks project/db and serves the status page.

Usage:
  pm.py serve [--port 8090]
  pm.py new <kind> <path> "<title>" [--type feature|bug|refactor] [--deps a/b,c] [--priority 100]
  pm.py next [n]
  pm.py brief <epic>/<task> [--files a,b] [--env name] [--model opus|sonnet] [--worktree]
  pm.py deliver <path> --tests <project>[:<suite>][,...] [--bench <project>[,...]] [--env name]
  pm.py start|discard|reset <path>
  pm.py done <path> [--force]
  pm.py status
  pm.py check

docs/PROCESS.md describes the loop these commands encode.
"""
import argparse
import datetime as dt
import http.server
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.parse

ROOT = os.path.dirname(os.path.abspath(__file__))
DB = os.path.join(ROOT, "db")
REPO = os.path.dirname(ROOT)
INTEGRATION_OUT = os.path.join(REPO, "test", "integration", "out")
INSPECTOR = os.path.join(REPO, "apps", "box_inspector", "index.html")
WASM_IMAGE = "deliverable/wasm/box_app.js"
DEFAULT_SCENE = "etc/scenes/primitives.flecs"
CONTENT_TYPES = {
    ".wasm": "application/wasm",
    ".js": "application/javascript",
    ".html": "text/html",
    ".png": "image/png",
    ".json": "application/json",
}
STATUSES = ("pending", "in progress", "complete", "discarded")
KINDS = ("bug", "feature", "refactor")
LEVELS = ("epic", "task", "subtask")
TEST_STATUSES = ("pass", "fail", "empty")
BENCH_METRICS = ("mean_ms", "p50_ms", "p99_ms", "max_ms", "fps")
BENCH_FIELDS = ("entities", "static", "dynamic", "frames", "drawn", "culled")
MICRO_FIELDS = ("median_ns", "ci_low_ns", "ci_high_ns", "mean_ns", "stddev_ns", "min_ns", "max_ns",
                "p95_ns", "p99_ns", "outliers", "iterations", "samples", "items_per_sec")
BENCH_PROJECTS = os.path.join(REPO, "bench")
BENCH_EXEMPT_EPIC = "project_tool"
BUILD_KINDS = ("debug", "release", "wasm")
BUILD_STEP_KINDS = ("compile", "link", "bundle", "discovery", "generate", "etc", "loc", "other")
BUILD_SETUP = "(build)"
TREND_POINTS = 12
TEST_TREND = ("pass", "fail", "empty", "cases", "elapsed")
MICRO_TREND = ("median_ns", "mean_ns", "cases")
MICRO_REPORT_TREND = MICRO_TREND + ("project",)
DONE_STATUSES = ("complete", "discarded")
PRIORITY_DEFAULT = 100
UNRANKED = 1 << 30
NEXT_UP = 10
TEST_JOBS = 8
REPORT_WORDS = 150
STRICT_TASK = "project_tool/process"
EPIC_SECTIONS = ("What was built", "Verification", "Lessons", "Not in this epic")
EPIC_README_LINES = 200
EPIC_WASM = ("wasm", "box_app.wasm")


def now():
    return dt.datetime.now(dt.timezone.utc).isoformat(timespec="milliseconds")


def read_json(path):
    try:
        with open(path) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def read_text(path):
    try:
        with open(path) as f:
            return f.read()
    except OSError:
        return None


def parse_readme(text):
    title = None
    brief = None
    if text:
        for line in text.splitlines():
            if title is None and line.startswith("# "):
                title = line[2:].strip()
                continue
            if title is not None and line.strip() and not line.startswith("#"):
                brief = line.strip()
                break
    return {"title": title, "brief": brief, "markdown": text or ""}


def list_deliverables(path):
    deliverable_dir = os.path.join(path, "deliverable")
    result = []
    if not os.path.isdir(deliverable_dir):
        return result
    for root, _, files in os.walk(deliverable_dir):
        for name in sorted(files):
            if name.startswith("."):
                continue
            full = os.path.join(root, name)
            rel = os.path.relpath(full, DB)
            result.append({
                "name": os.path.relpath(full, deliverable_dir),
                "url": "/deliverable/" + rel.replace(os.sep, "/"),
                "size": os.path.getsize(full),
                "tests": summarize_tests(full) if name.endswith(".json") else None,
                "build": summarize_build(full) if name.endswith(".json") else None,
            })
    return result


def content_type(path):
    return CONTENT_TYPES.get(os.path.splitext(path)[1].lower(), "text/plain")


def inspect_urls(path, rel):
    image = os.path.join(path, *WASM_IMAGE.split("/"))
    if not os.path.isfile(image):
        return None, None
    wasm_url = "/deliverable/" + rel + "/" + WASM_IMAGE
    return wasm_url, "/inspector?wasm=" + wasm_url + "&scene=" + DEFAULT_SCENE


def summarize_tests(path):
    data = read_json(path)
    if not isinstance(data, dict) or "tests" not in data:
        return None
    return {
        "project": data.get("project"),
        "pass": data.get("pass", 0),
        "fail": data.get("fail", 0),
        "empty": data.get("empty", 0),
        "elapsed": data.get("elapsed", 0),
        "timestamp": data.get("timestamp"),
        "count": len(data.get("tests", [])),
    }


def is_build_data(data):
    """Tell a bake3 --build-json report from any other report in a deliverable folder."""
    return (isinstance(data, dict) and isinstance(data.get("steps"), list)
            and isinstance(data.get("totals"), dict) and "total_sec" in data)


def build_kind(data):
    """Name the kind of a build report: wasm for the web target, else its cfg."""
    target = data.get("target") if isinstance(data.get("target"), str) else ""
    if target == "em" or target.lower().startswith("wasm"):
        return "wasm"
    cfg = data.get("cfg")
    return cfg if isinstance(cfg, str) and cfg else "build"


def build_files(totals):
    """Count the files the projects of a build report compiled."""
    return sum(number(row.get("files")) for row in totals.values() if isinstance(row, dict))


def loc_code(loc):
    """Read the code line count out of a totals loc block, 0 when cloc left no counts."""
    return number(loc.get("code")) if isinstance(loc, dict) else 0


def summarize_build(path):
    """Summarize a build report for the deliverable listing of an item."""
    data = read_json(path)
    if not is_build_data(data):
        return None
    totals = data.get("totals") or {}
    git = data.get("git") if isinstance(data.get("git"), dict) else {}
    kinds = totals.get("kind") if isinstance(totals.get("kind"), dict) else {}
    return {
        "kind": build_kind(data),
        "cfg": data.get("cfg"),
        "target": data.get("target"),
        "ok": data.get("ok") is True,
        "total_sec": number(data.get("total_sec")),
        "files": build_files(totals.get("project") or {}),
        "compile_sec": number(kinds.get("compile")),
        "link_sec": number(kinds.get("link")),
        "loc": loc_code(totals.get("loc")),
        "sha": (git.get("sha") or "")[:7],
        "timestamp": data.get("timestamp"),
        "steps": len(data.get("steps") or []),
    }


def child_dirs(path):
    for name in sorted(os.listdir(path)):
        full = os.path.join(path, name)
        if name == "deliverable" or name.startswith(".") or not os.path.isdir(full):
            continue
        yield name, full


def priority_of(progress):
    value = progress.get("priority")
    if isinstance(value, bool) or not isinstance(value, int):
        return PRIORITY_DEFAULT
    return value


def set_priority(name, value):
    directory = os.path.join(DB, name)
    progress = read_json(os.path.join(directory, "progress.json"))
    if progress is None:
        return f"unknown epic '{name}'"
    progress["priority"] = value
    write_progress(directory, progress)
    return None


def start_order(item, rank=None):
    started = item["start_time"] or ""
    if started:
        return (0, started, 0, "", item["name"])
    key = (rank or {}).get(item.get("path") or item["name"], UNRANKED)
    return (1, "", key, item["created"] or "", item["name"])


def plan_items(epics):
    items = []
    for epic in epics:
        for task in epic["children"]:
            for item in [task] + list(task["children"]):
                items.append({
                    "path": item["path"],
                    "title": item["readme"]["title"] or item["name"],
                    "epic": epic["name"],
                    "epic_title": epic["readme"]["title"] or epic["name"],
                    "level": item["level"],
                    "status": item["status"],
                    "created": item["created"] or "",
                    "start_time": item["start_time"],
                    "dependencies": [str(d) for d in item["dependencies"]],
                })
    return items


def derived_plan(epics, limit=NEXT_UP):
    """Order the unfinished tasks: dependencies first, then epic priority, then creation time."""
    priority = {e["name"]: e["priority"] for e in epics}
    items = plan_items(epics)
    known = {item["path"] for item in items}
    done = {item["path"] for item in items if item["status"] in DONE_STATUSES}
    left = {name: 0 for name in priority}
    for item in items:
        if item["path"] not in done:
            left[item["epic"]] = left.get(item["epic"], 0) + 1
    epic_done = {e["name"] for e in epics if e["status"] == "complete"}
    epic_done |= {name for name, count in left.items() if not count}

    def satisfied(dep):
        if dep in known:
            return dep in done
        if "/" not in dep and dep in priority:
            return dep in epic_done
        return True

    def ready(item):
        return all(satisfied(dep) for dep in item["dependencies"])

    pending = [item for item in items if item["path"] not in done]
    order, first = [], None
    while pending:
        candidates = [item for item in pending if ready(item)]
        if not candidates:
            break
        if first is None:
            first = {item["path"] for item in candidates}
        pick = min(candidates, key=lambda i: (priority.get(i["epic"], PRIORITY_DEFAULT), i["created"], i["path"]))
        order.append(pick)
        done.add(pick["path"])
        pending.remove(pick)
        left[pick["epic"]] -= 1
        if not left[pick["epic"]]:
            epic_done.add(pick["epic"])
    first = first or set()
    next_up = [item for item in order if item["path"] in first and not item["start_time"]][:limit]
    return {"order": order, "next_up": next_up, "blocked": sorted(item["path"] for item in pending)}


def plan_rank(epics, plan):
    rank = {e["name"]: e["priority"] for e in epics}
    for i, item in enumerate(plan["order"]):
        rank[item["path"]] = i
    return rank


def sort_tree(items, rank):
    items.sort(key=lambda item: start_order(item, rank))
    for item in items:
        sort_tree(item["children"], rank)


def walk_item(path, level, rel):
    progress = read_json(os.path.join(path, "progress.json")) or {}
    item = {
        "name": os.path.basename(path),
        "path": rel,
        "level": LEVELS[level],
        "readme": parse_readme(read_text(os.path.join(path, "README.md"))),
        "kind": progress.get("kind", "feature"),
        "status": progress.get("status", "pending"),
        "start_time": progress.get("start_time"),
        "end_time": progress.get("end_time"),
        "dependencies": progress.get("dependencies", []),
        "created": progress.get("created"),
        "bench": progress.get("bench", True),
        "deliverables": list_deliverables(path),
        "children": [],
    }
    if item["level"] == "epic":
        item["priority"] = priority_of(progress)
        item["wasm_url"], item["inspect_url"] = inspect_urls(path, rel)
    if level + 1 < len(LEVELS):
        for name, full in child_dirs(path):
            item["children"].append(walk_item(full, level + 1, rel + "/" + name))
    return item


def mtime_iso(path):
    try:
        stamp = os.path.getmtime(path)
    except OSError:
        return None
    return dt.datetime.fromtimestamp(stamp, dt.timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z")


def report_time(path, data):
    stamp = data.get("timestamp") if isinstance(data, dict) else None
    if isinstance(stamp, str) and stamp.strip():
        return stamp.strip(), "report"
    return mtime_iso(path), "mtime"


def time_value(stamp):
    if not isinstance(stamp, str):
        return 0.0
    try:
        return dt.datetime.fromisoformat(stamp.replace("Z", "+00:00")).timestamp()
    except ValueError:
        return 0.0


def number(value, default=0):
    return value if isinstance(value, (int, float)) and not isinstance(value, bool) else default


def deliverable_reports():
    result = []
    if not os.path.isdir(DB):
        return result
    for root, dirs, files in os.walk(DB):
        dirs.sort()
        parts = os.path.relpath(root, DB).split(os.sep)
        if "deliverable" not in parts:
            continue
        for name in sorted(files):
            if name.startswith(".") or not name.endswith(".json"):
                continue
            full = os.path.join(root, name)
            data = read_json(full)
            if not isinstance(data, dict):
                continue
            rel = os.path.relpath(full, DB).replace(os.sep, "/")
            result.append({
                "item": "/".join(parts[:parts.index("deliverable")]),
                "file": rel,
                "url": "/deliverable/" + rel,
                "full": full,
                "data": data,
            })
    return result


def trend_tail(points, fields):
    """Keep the last TREND_POINTS of a history as compact {time, field...} rows."""
    tail = []
    for point in points[-TREND_POINTS:]:
        row = {"time": point.get("time")}
        row.update({field: point.get(field) for field in fields})
        tail.append(row)
    return tail


def test_status(row):
    """Sum up a test level: a failure wins, then a pass, otherwise the level is empty."""
    if number(row.get("fail")):
        return "fail"
    if number(row.get("pass")):
        return "pass"
    return "empty"


def suite_rows(data):
    suites = {}
    for case in data.get("tests") or []:
        if not isinstance(case, dict):
            continue
        name = case.get("suite") or "(none)"
        row = suites.setdefault(name, {
            "suite": name, "cases": 0, "pass": 0, "fail": 0, "empty": 0, "time": 0.0, "case_list": [],
        })
        status = case.get("status") if case.get("status") in TEST_STATUSES else "empty"
        elapsed = round(number(case.get("elapsed")), 6)
        row["cases"] += 1
        row[status] += 1
        row["time"] = round(row["time"] + elapsed, 6)
        row["case_list"].append({"case": case.get("case"), "status": status, "time": elapsed})
    return [suites[name] for name in sorted(suites)]


def aggregate_tests():
    reports = []
    for entry in deliverable_reports():
        data = entry["data"]
        if not isinstance(data.get("tests"), list):
            continue
        stamp, source = report_time(entry["full"], data)
        suites = suite_rows(data)
        counts = {key: sum(s[key] for s in suites) for key in TEST_STATUSES}
        for key in TEST_STATUSES:
            if key in data:
                counts[key] = number(data.get(key))
        reports.append({
            "item": entry["item"],
            "file": entry["file"],
            "url": entry["url"],
            "project": data.get("project") or "unknown",
            "time": stamp,
            "time_source": source,
            "cases": sum(s["cases"] for s in suites),
            "elapsed": round(number(data.get("elapsed")), 6),
            "suites": [{k: v for k, v in s.items() if k != "case_list"} for s in suites],
            "case_time": round(sum(s["time"] for s in suites), 6),
            **counts,
        })
        reports[-1]["suite_count"] = len(suites)
        for suite in suites:
            reports[-1].setdefault("_suites", []).append(suite)
    reports.sort(key=lambda r: (time_value(r["time"]), r["file"]))

    latest = {}
    suite_history = {}
    case_history = {}
    for report in reports:
        for suite in report.pop("_suites", []):
            key = report["project"] + "|" + suite["suite"]
            point = {
                "time": report["time"],
                "time_source": report["time_source"],
                "item": report["item"],
                "url": report["url"],
                "cases": suite["cases"],
                "elapsed": suite["time"],
            }
            point.update({status: suite[status] for status in TEST_STATUSES})
            suite_history.setdefault(key, []).append(point)
            row = dict(suite)
            row.update({
                "key": key,
                "project": report["project"],
                "item": report["item"],
                "file": report["file"],
                "url": report["url"],
                "report_time": report["time"],
                "time_source": report["time_source"],
                "cases_detail": row.pop("case_list"),
            })
            for case in row["cases_detail"]:
                case_history.setdefault(key + "|" + str(case["case"]), []).append({
                    "time": report["time"],
                    "time_source": report["time_source"],
                    "item": report["item"],
                    "status": case["status"],
                    "elapsed": case["time"],
                })
            latest[key] = row
    suites = [latest[key] for key in sorted(latest)]
    for suite in suites:
        points = suite_history.get(suite["key"], [])
        suite["reports"] = len(points)
        suite["status"] = test_status(suite)
        suite["trend"] = trend_tail(points, TEST_TREND)

    by_project = {}
    for report in reports:
        by_project.setdefault(report["project"], []).append(report)
    projects = {}
    for suite in suites:
        row = projects.setdefault(suite["project"], {
            "project": suite["project"], "suites": 0, "cases": 0, "pass": 0, "fail": 0, "empty": 0, "time": 0.0,
        })
        row["suites"] += 1
        row["cases"] += suite["cases"]
        row["time"] = round(row["time"] + suite["time"], 6)
        for key in TEST_STATUSES:
            row[key] += suite[key]
    for name, row in projects.items():
        history = by_project.get(name, [])
        last = history[-1] if history else {}
        row["reports"] = len(history)
        row["latest"] = last.get("time")
        row["item"] = last.get("item")
        row["url"] = last.get("url")
        row["status"] = test_status(row)
        row["trend"] = trend_tail(history, TEST_TREND)
    project_rows = [projects[name] for name in sorted(projects)]
    overall = {
        "projects": len(project_rows),
        "suites": len(suites),
        "reports": len(reports),
        "cases": sum(row["cases"] for row in project_rows),
        "time": round(sum(row["time"] for row in project_rows), 6),
        "latest": reports[-1]["time"] if reports else None,
        "trend": trend_tail(reports, TEST_TREND),
    }
    for key in TEST_STATUSES:
        overall[key] = sum(row[key] for row in project_rows)
    overall["status"] = test_status(overall)
    return {
        "generated": now(),
        "overall": overall,
        "reports": reports,
        "suites": suites,
        "suite_history": suite_history,
        "case_history": case_history,
        "projects": project_rows,
    }


def bench_runs(full, data, origin, item, url):
    if not isinstance(data, dict):
        return []
    if isinstance(data.get("results"), list):
        runs = []
        for result in data["results"]:
            runs.extend(bench_runs(full, result, origin, item, url))
        return runs
    if not any(key in data for key in BENCH_METRICS):
        return []
    source = data.get("scene") if isinstance(data.get("scene"), str) else None
    name = os.path.splitext(os.path.basename(source))[0] if source else os.path.splitext(os.path.basename(full))[0]
    stamp, time_source = report_time(full, data)
    run = {
        "scene": name,
        "source": source,
        "origin": origin,
        "item": item,
        "file": os.path.relpath(full, REPO).replace(os.sep, "/"),
        "url": url,
        "time": stamp,
        "time_source": time_source,
    }
    for key in BENCH_METRICS + BENCH_FIELDS:
        if key in data:
            run[key] = number(data.get(key), None)
    return [run]


def micro_runs(full, data, item, url):
    if not isinstance(data, dict) or not isinstance(data.get("benchmarks"), list):
        return []
    project = data.get("project") or os.path.splitext(os.path.basename(full))[0]
    stamp, time_source = report_time(full, data)
    runs = []
    for case in data["benchmarks"]:
        if not isinstance(case, dict):
            continue
        suite = case.get("suite") or "(none)"
        name = case.get("case") or "(none)"
        run = {
            "key": f"{project}/{suite}.{name}",
            "project": project,
            "suite": suite,
            "case": name,
            "item": item,
            "file": os.path.relpath(full, REPO).replace(os.sep, "/"),
            "url": url,
            "time": stamp,
            "time_source": time_source,
        }
        for key in MICRO_FIELDS:
            if key in case:
                run[key] = number(case.get(key), None)
        runs.append(run)
    return runs


def micro_case_row(run):
    row = {"case": run["case"]}
    row.update({field: run[field] for field in MICRO_FIELDS if field in run})
    return row


def micro_report_points(micro):
    """Fold the case runs of every benchmark report into one point per project, oldest first."""
    points = {}
    for run in micro:
        point = points.setdefault((run["project"], run["file"]), {
            "time": run["time"], "time_source": run["time_source"], "item": run["item"], "url": run["url"],
            "project": run["project"], "file": run["file"], "cases": 0, "suites": 0,
            "median_ns": 0.0, "mean_ns": 0.0, "_suites": [],
        })
        point["cases"] += 1
        point["median_ns"] = round(point["median_ns"] + number(run.get("median_ns")), 3)
        point["mean_ns"] = round(point["mean_ns"] + number(run.get("mean_ns")), 3)
        if run["suite"] not in point["_suites"]:
            point["_suites"].append(run["suite"])
    history = {}
    for point in points.values():
        point["suites"] = len(point.pop("_suites"))
        history.setdefault(point["project"], []).append(point)
    for rows in history.values():
        rows.sort(key=lambda p: time_value(p["time"]))
    return history


def micro_aggregates(micro):
    reports = {}
    case_history = {}
    for run in micro:
        reports.setdefault((run["file"], run["project"], run["suite"]), []).append(run)
        key = run["project"] + "|" + run["suite"] + "|" + run["case"]
        point = {"time": run["time"], "time_source": run["time_source"], "item": run["item"], "url": run["url"]}
        point.update({field: run[field] for field in MICRO_FIELDS if field in run})
        case_history.setdefault(key, []).append(point)
    suite_history = {}
    latest = {}
    for (_, project, suite), rows in reports.items():
        key = project + "|" + suite
        point = {
            "time": rows[0]["time"],
            "time_source": rows[0]["time_source"],
            "item": rows[0]["item"],
            "file": rows[0]["file"],
            "url": rows[0]["url"],
            "cases": len(rows),
            "median_ns": round(sum(number(row.get("median_ns")) for row in rows), 3),
            "mean_ns": round(sum(number(row.get("mean_ns")) for row in rows), 3),
        }
        suite_history.setdefault(key, []).append(point)
        latest[key] = dict(point, key=key, project=project, suite=suite,
                           cases_detail=[micro_case_row(row) for row in rows])
    for points in list(suite_history.values()) + list(case_history.values()):
        points.sort(key=lambda p: time_value(p["time"]))
    for key, row in latest.items():
        points = suite_history.get(key, [])
        row["reports"] = len(points)
        row["trend"] = trend_tail(points, MICRO_TREND)
    project_history = micro_report_points(micro)
    projects = {}
    for key in sorted(latest):
        row = latest[key]
        project = projects.setdefault(row["project"], {
            "project": row["project"], "suites": 0, "cases": 0, "median_ns": 0.0, "mean_ns": 0.0,
            "time": row["time"], "item": row["item"], "url": row["url"],
        })
        project["suites"] += 1
        project["cases"] += row["cases"]
        project["median_ns"] = round(project["median_ns"] + row["median_ns"], 3)
        project["mean_ns"] = round(project["mean_ns"] + row["mean_ns"], 3)
        if time_value(row["time"]) >= time_value(project["time"]):
            project.update({"time": row["time"], "item": row["item"], "url": row["url"]})
    for name, project in projects.items():
        points = project_history.get(name, [])
        project["reports"] = len(points)
        project["latest"] = project["time"]
        project["trend"] = trend_tail(points, MICRO_REPORT_TREND)
    project_rows = [projects[name] for name in sorted(projects)]
    runs = sorted((point for points in project_history.values() for point in points),
                  key=lambda p: time_value(p["time"]))
    return {
        "micro_overall": {
            "projects": len(project_rows),
            "suites": len(latest),
            "cases": sum(row["cases"] for row in project_rows),
            "reports": len(runs),
            "median_ns": round(sum(row["median_ns"] for row in project_rows), 3),
            "mean_ns": round(sum(row["mean_ns"] for row in project_rows), 3),
            "latest": runs[-1]["time"] if runs else None,
            "trend": trend_tail(runs, MICRO_REPORT_TREND),
        },
        "micro_projects": project_rows,
        "micro_suites": [latest[key] for key in sorted(latest)],
        "micro_suite_history": suite_history,
        "micro_case_history": case_history,
    }


def is_bench_report(name):
    return name.startswith("bench") and name.endswith(".json")


def bench_projects_time():
    stamps = []
    if os.path.isdir(BENCH_PROJECTS):
        for name in sorted(os.listdir(BENCH_PROJECTS)):
            config = os.path.join(BENCH_PROJECTS, name, "project.json")
            if os.path.isfile(config):
                stamps.append(os.path.getmtime(config))
    return min(stamps) if stamps else None


def aggregate_benchmarks(extra_dirs=None):
    runs = []
    micro = []
    for entry in deliverable_reports():
        runs.extend(bench_runs(entry["full"], entry["data"], "db", entry["item"], entry["url"]))
        micro.extend(micro_runs(entry["full"], entry["data"], entry["item"], entry["url"]))
    for directory in (INTEGRATION_OUT,) if extra_dirs is None else extra_dirs:
        if not os.path.isdir(directory):
            continue
        for name in sorted(os.listdir(directory)):
            if name.startswith(".") or not name.endswith(".json"):
                continue
            full = os.path.join(directory, name)
            item = os.path.relpath(directory, REPO).replace(os.sep, "/")
            data = read_json(full)
            runs.extend(bench_runs(full, data, "integration", item, None))
            micro.extend(micro_runs(full, data, item, None))
    runs.sort(key=lambda r: (time_value(r["time"]), r["scene"], r["file"]))
    micro.sort(key=lambda r: (time_value(r["time"]), r["key"], r["file"]))
    latest = {}
    for run in runs:
        latest[run["scene"]] = run
    latest_micro = {}
    for run in micro:
        latest_micro[run["key"]] = run
    return {
        "generated": now(),
        "runs": runs,
        "scenes": [latest[name] for name in sorted(latest)],
        "micro_runs": micro,
        "micro": [latest_micro[key] for key in sorted(latest_micro)],
        **micro_aggregates(micro),
    }


def build_step_nodes(steps):
    """Turn the step tree of a build report into rows, slowest first, keeping the nesting."""
    rows = []
    for step in steps or []:
        if not isinstance(step, dict):
            continue
        rows.append({
            "name": step.get("name") or "(step)",
            "kind": step.get("kind") or "other",
            "project": step.get("project"),
            "time": round(number(step.get("duration_sec")), 6),
            "start": round(number(step.get("start_sec")), 6),
            "ok": step.get("ok") is not False,
            "object": step.get("object"),
            "steps": build_step_nodes(step.get("children")),
        })
    rows.sort(key=lambda row: (-row["time"], row["name"]))
    return rows


def count_steps(rows):
    return sum(1 + count_steps(row["steps"]) for row in rows)


def build_project_rows(data):
    """Group the steps of a build report per project, with the setup steps in their own row."""
    totals = (data.get("totals") or {}).get("project") or {}
    rows = []
    setup = []
    for step in data.get("steps") or []:
        if not isinstance(step, dict):
            continue
        if step.get("kind") != "project":
            setup.extend(build_step_nodes([step]))
            continue
        name = step.get("project") or step.get("name") or "(project)"
        summary = totals.get(name) if isinstance(totals.get(name), dict) else {}
        rows.append({
            "project": name,
            "time": round(number(summary.get("total_sec"), number(step.get("duration_sec"))), 6),
            "compile_sec": round(number(summary.get("compile_sec")), 6),
            "link_sec": round(number(summary.get("link_sec")), 6),
            "files": number(summary.get("files")),
            "loc": loc_code(summary.get("loc")) if "loc" in summary else None,
            "ok": step.get("ok") is not False,
            "steps": build_step_nodes(step.get("children")),
        })
    if setup:
        setup.sort(key=lambda row: (-row["time"], row["name"]))
        rows.insert(0, {"project": BUILD_SETUP, "time": round(sum(row["time"] for row in setup), 6),
                        "compile_sec": 0.0, "link_sec": 0.0, "files": 0, "loc": None,
                        "ok": all(row["ok"] for row in setup), "steps": setup})
    return rows


def build_reports():
    """Read every bake3 build report under project/db, oldest first."""
    reports = []
    for entry in deliverable_reports():
        data = entry["data"]
        if not is_build_data(data):
            continue
        totals = data.get("totals") or {}
        kinds = totals.get("kind") if isinstance(totals.get("kind"), dict) else {}
        loc_totals = totals.get("loc") if isinstance(totals.get("loc"), dict) else {}
        git = data.get("git") if isinstance(data.get("git"), dict) else {}
        host = data.get("host") if isinstance(data.get("host"), dict) else {}
        projects = build_project_rows(data)
        stamp, source = report_time(entry["full"], data)
        reports.append({
            "item": entry["item"], "file": entry["file"], "url": entry["url"],
            "kind": build_kind(data), "cfg": data.get("cfg"), "target": data.get("target"),
            "environment": data.get("environment"), "time": stamp, "time_source": source,
            "total_sec": round(number(data.get("total_sec")), 6), "ok": data.get("ok") is True,
            "files": build_files(totals.get("project") or {}),
            "compile_sec": round(number(kinds.get("compile")), 6),
            "link_sec": round(number(kinds.get("link")), 6),
            "loc": loc_code(loc_totals),
            "loc_note": loc_totals.get("note") if isinstance(loc_totals.get("note"), str) else None,
            "kinds": {key: round(number(kinds.get(key)), 6) for key in BUILD_STEP_KINDS if key in kinds},
            "sha": git.get("sha") or "", "short_sha": (git.get("sha") or "")[:7],
            "dirty": bool(git.get("dirty")), "branch": git.get("branch"),
            "os": host.get("os"), "cpu_count": number(host.get("cpu_count")),
            "steps": sum(count_steps(row["steps"]) for row in projects), "projects": projects,
        })
    reports.sort(key=lambda report: (time_value(report["time"]), report["kind"], report["file"]))
    return reports


def build_histories(reports):
    """Fold the reports into a history per kind and per kind and project, oldest first."""
    kinds = {}
    projects = {}
    for report in reports:
        kinds.setdefault(report["kind"], []).append({
            "time": report["time"], "item": report["item"], "kind": report["kind"], "url": report["url"],
            "total_sec": report["total_sec"], "files": report["files"], "sha": report["short_sha"],
            "compile_sec": report["compile_sec"], "link_sec": report["link_sec"], "loc": report["loc"],
        })
        for row in report["projects"]:
            projects.setdefault(report["kind"] + "|" + row["project"], []).append({
                "time": report["time"], "item": report["item"], "project": row["project"],
                "total_sec": row["time"], "compile_sec": row["compile_sec"],
                "link_sec": row["link_sec"], "files": row["files"], "loc": row["loc"],
            })
    kinds = {key: rows[-TREND_POINTS:] for key, rows in kinds.items()}
    return kinds, {key: rows[-TREND_POINTS:] for key, rows in projects.items()}


def build_kind_rows(reports, history):
    """Summarize the latest report of every build kind, debug, release and wasm first."""
    rows = []
    for kind in sorted({report["kind"] for report in reports}):
        own = [report for report in reports if report["kind"] == kind]
        latest = own[-1]
        rows.append({
            "kind": kind, "reports": len(own), "latest": latest["time"], "item": latest["item"],
            "url": latest["url"], "file": latest["file"], "total_sec": latest["total_sec"],
            "files": latest["files"], "compile_sec": latest["compile_sec"], "link_sec": latest["link_sec"],
            "sha": latest["short_sha"], "ok": all(report["ok"] for report in own),
            "loc": latest["loc"], "loc_delta": (latest["loc"] - own[-2]["loc"]) if len(own) > 1 else None,
            "trend": history.get(kind, []),
        })
    rows.sort(key=lambda row: (BUILD_KINDS.index(row["kind"]) if row["kind"] in BUILD_KINDS
                               else len(BUILD_KINDS), row["kind"]))
    return rows


def build_overall(reports, kinds):
    """Summarize every build report: the latest one plus the total of the latest per kind."""
    latest = reports[-1] if reports else {}
    names = {row["project"] for report in reports for row in report["projects"] if row["project"] != BUILD_SETUP}
    trend = sorted((point for row in kinds for point in row["trend"]), key=lambda p: time_value(p["time"]))
    return {
        "reports": len(reports), "kinds": len(kinds), "projects": len(names),
        "steps": sum(report["steps"] for report in reports),
        "files": latest.get("files", 0), "compile_sec": latest.get("compile_sec", 0),
        "link_sec": latest.get("link_sec", 0),
        "total_sec": round(sum(row["total_sec"] for row in kinds), 6),
        "sha": latest.get("short_sha", ""), "dirty": latest.get("dirty", False),
        "branch": latest.get("branch"), "latest": latest.get("time"),
        "ok": all(report["ok"] for report in reports) if reports else True,
        "loc": latest.get("loc", 0),
        "loc_delta": (reports[-1]["loc"] - reports[-2]["loc"]) if len(reports) > 1 else None,
        "trend": trend,
    }


def aggregate_builds():
    reports = build_reports()
    kind_history, project_history = build_histories(reports)
    kinds = build_kind_rows(reports, kind_history)
    return {
        "generated": now(), "reports": reports, "kinds": kinds, "kind_history": kind_history,
        "project_history": project_history, "overall": build_overall(reports, kinds),
    }


def collect_state():
    epics = load_epics()
    plan = derived_plan(epics)
    sort_tree(epics, plan_rank(epics, plan))
    return {"generated": now(), "epics": epics, "next_up": plan["next_up"]}


class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        route = urllib.parse.unquote(parsed.path)
        if route == "/" or route == "/index.html":
            return self.send_file(os.path.join(ROOT, "index.html"), "text/html; charset=utf-8")
        if route == "/api/state":
            return self.send_json(collect_state())
        if route == "/api/tests":
            return self.send_json(aggregate_tests())
        if route == "/api/benchmarks":
            return self.send_json(aggregate_benchmarks())
        if route == "/api/builds":
            return self.send_json(aggregate_builds())
        if route == "/inspector":
            if not os.path.isfile(INSPECTOR):
                return self.send_error(404, "apps/box_inspector/index.html is not there yet")
            return self.send_file(INSPECTOR, "text/html; charset=utf-8")
        if route.startswith("/deliverable/"):
            rel = os.path.normpath(route[len("/deliverable/"):])
            full = os.path.join(DB, rel)
            if rel.startswith("..") or "/deliverable/" not in full + "/" or not os.path.isfile(full):
                return self.send_error(404)
            return self.send_file(full, content_type(full))
        return self.send_error(404)

    def do_POST(self):
        route = urllib.parse.unquote(urllib.parse.urlparse(self.path).path)
        if route != "/api/priority":
            return self.send_error(404)
        try:
            length = int(self.headers.get("Content-Length") or 0)
            payload = json.loads(self.rfile.read(length) or b"null")
        except ValueError:
            return self.send_json({"error": "body is not valid json"}, 400)
        if not isinstance(payload, dict):
            return self.send_json({"error": "expected an object with 'epic' and 'priority'"}, 400)
        name = payload.get("epic")
        value = payload.get("priority")
        if not isinstance(name, str) or not re.fullmatch(r"[a-z0-9_]+", name or ""):
            return self.send_json({"error": f"'{name}' is not an epic name"}, 400)
        if isinstance(value, bool) or not isinstance(value, int):
            return self.send_json({"error": "priority must be an integer"}, 400)
        error = set_priority(name, value)
        if error:
            return self.send_json({"error": error}, 400)
        return self.send_json({"epic": name, "priority": value})

    def send_json(self, payload, status=200):
        body = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_file(self, path, ctype):
        try:
            with open(path, "rb") as f:
                body = f.read()
        except OSError:
            return self.send_error(404)
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def cmd_serve(args):
    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    print(f"box project page: http://127.0.0.1:{args.port}/")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


def item_dir(path):
    parts = [p for p in path.strip("/").split("/") if p]
    if not parts or len(parts) > 3:
        sys.exit(f"invalid item path '{path}', expected epic[/task[/subtask]]")
    for part in parts:
        if not re.fullmatch(r"[a-z0-9_]+", part):
            sys.exit(f"invalid path segment '{part}', use lowercase letters, digits and underscores")
    return os.path.join(DB, *parts), parts


def write_json(path, data):
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(data, f, indent=2)
        f.write("\n")
    os.replace(tmp, path)


def write_progress(directory, progress):
    write_json(os.path.join(directory, "progress.json"), progress)


def cmd_new(args):
    directory, parts = item_dir(args.path)
    if args.kind != LEVELS[len(parts) - 1]:
        sys.exit(f"path '{args.path}' has depth {len(parts)}, which is a {LEVELS[len(parts) - 1]}, not a {args.kind}")
    if os.path.exists(directory):
        sys.exit(f"{args.path} already exists")
    if len(parts) > 1 and not os.path.isdir(os.path.dirname(directory)):
        sys.exit(f"parent of {args.path} does not exist")
    os.makedirs(directory)
    deps = [d.strip() for d in (args.deps or "").split(",") if d.strip()]
    progress = {
        "kind": args.type,
        "status": "pending",
        "created": now(),
        "start_time": None,
        "end_time": None,
        "dependencies": deps,
    }
    if len(parts) == 1:
        progress["priority"] = PRIORITY_DEFAULT if getattr(args, "priority", None) is None else args.priority
    write_progress(directory, progress)
    with open(os.path.join(directory, "README.md"), "w") as f:
        f.write(f"# {args.title}\n\n{args.brief or 'TODO: brief description.'}\n\n## Details\n\nTODO\n")
    if len(parts) < 3:
        os.makedirs(os.path.join(directory, "deliverable"))
    print(f"created {args.kind} {args.path}")


def set_status(path, status):
    directory, _ = item_dir(path)
    progress = read_json(os.path.join(directory, "progress.json"))
    if progress is None:
        sys.exit(f"{path} has no progress.json")
    progress["status"] = status
    if status == "in progress":
        progress["start_time"] = progress.get("start_time") or now()
        progress["end_time"] = None
    elif status in ("complete", "discarded"):
        progress["start_time"] = progress.get("start_time") or now()
        progress["end_time"] = now()
    else:
        progress["start_time"] = None
        progress["end_time"] = None
    write_progress(directory, progress)
    print(f"{path}: {status}")


def cmd_status(args):
    def show(item, depth):
        marker = {"pending": " ", "in progress": ">", "complete": "x", "discarded": "-"}[item["status"]]
        print(f"{'  ' * depth}[{marker}] {item['name']}  {item['readme']['title'] or ''}")
        for child in item["children"]:
            show(child, depth + 1)
    for epic in collect_state()["epics"]:
        show(epic, 0)


def check_priorities(epics, problems, warnings):
    by_name = {e["name"]: e for e in epics}
    for name in sorted(by_name):
        epic = by_name[name]
        for dep in epic["dependencies"]:
            other = by_name.get(str(dep).split("/")[0])
            if other is None or other["name"] == name or epic["priority"] >= other["priority"]:
                continue
            warnings.append(f"{name}: warning: priority {epic['priority']} runs before its "
                            f"dependency {other['name']} ({other['priority']})")
    for path in derived_plan(epics)["blocked"]:
        problems.append(f"{path}: dependency cycle, this task can never become ready")


def cmd_check(args):
    problems = []
    warnings = []
    known = set()
    bench_since = bench_projects_time()
    strict = strict_since()

    def wants_bench(item):
        if bench_since is None or item["level"] != "task" or item["status"] != "complete":
            return False
        if item["path"].split("/")[0] == BENCH_EXEMPT_EPIC or item["bench"] is False:
            return False
        return time_value(item["end_time"]) > bench_since

    def visit(item):
        known.add(item["path"])
        readme = item["readme"]
        if not readme["title"]:
            problems.append(f"{item['path']}: README.md has no title")
        if not readme["brief"]:
            problems.append(f"{item['path']}: README.md has no brief description")
        if item["status"] not in STATUSES:
            problems.append(f"{item['path']}: invalid status '{item['status']}'")
        if item["kind"] not in KINDS:
            problems.append(f"{item['path']}: invalid kind '{item['kind']}'")
        if item["status"] == "complete" and item["level"] != "subtask":
            has_tests = any(d["tests"] for d in item["deliverables"])
            if not has_tests:
                problems.append(f"{item['path']}: complete without a test result deliverable")
        if item["status"] == "complete" and item["level"] == "epic":
            names = [d["name"] for d in item["deliverables"]]
            if "README.md" not in names:
                problems.append(f"{item['path']}: complete epic without deliverable/README.md")
            if strict is not None and time_value(item["end_time"]) > strict:
                problems.extend(f"{item['path']}: missing {gap}" for gap in deliverable_gaps(item))
        if item["status"] == "complete" and item["level"] == "task":
            warnings.extend(f"{item['path']}: warning: missing {gap}"
                            for gap in deliverable_gaps(item, bench=False))
        if wants_bench(item) and not any(is_bench_report(d["name"]) for d in item["deliverables"]):
            warnings.append(f"{item['path']}: warning: complete without deliverable/bench.json")
        for child in item["children"]:
            visit(child)

    state = collect_state()
    for epic in state["epics"]:
        visit(epic)

    def visit_deps(item):
        for dep in item["dependencies"]:
            if dep not in known:
                problems.append(f"{item['path']}: unknown dependency '{dep}'")
        for child in item["children"]:
            visit_deps(child)

    for epic in state["epics"]:
        visit_deps(epic)
    check_priorities(state["epics"], problems, warnings)
    for problem in problems:
        print(problem)
    for warning in warnings:
        print(warning)
    print(f"{len(problems)} problem(s), {len(warnings)} warning(s)")
    return 1 if problems else 0


BRIEF_HEADER = """You are implementing the task `{path}` of the Box project at `{repo}`.

Read `README.md` for the build, test and benchmark commands, `docs/ARCHITECTURE.md` for the
module layout and `design/DESIGN.md` for the brief. Everything you need is below: the task
spec, the epic it belongs to, the reports of the work it builds on, and the rules that apply
to every change. Do the task and nothing else; propose follow up work instead of doing it."""

RULE_ENV = """Build and test in your own environment so a parallel agent cannot corrupt your
objects:

```sh
bake3 --local-env={env}
bake3 run test/core --local-env={env} -- -j 8
```

Never build in the shared environment and never pass a different `--local-env`."""

RULE_FILES = """You own: {files}.

Never create or edit a file outside that list; if the task needs one, stop and report it.
A file you share with another agent is re-read right before you edit it, because it may
have changed since you last looked at it. Do not touch `project/db` outside your own task
folder."""

RULE_STYLE = """`design/coding_style.c` is the reference and `python3 tools/lint.py` enforces it.
It must report 0 problems before every commit; `python3 tools/lint.py --fix` reformats
signatures and blank lines. The rules: a file header with `@file` and `@brief`, a doc
comment with `@param` and `@return` on every static function and header prototype, one
parameter per line and the opening brace on its own line, no comments inside function
bodies (a test may start with one `// epic/task` line), a blank line before `return` and
after every block, no `return` inside a nested block (use `goto done`), lines under 100
characters, functions under 200 lines, files under 2000 lines, and a doc comment on every
type and member."""

RULE_FLECS = """- This flecs has no implicit defer mode. Inside an OnAdd or OnRemove observer the
  entity is mid table-move: structural changes to it (add, set of a missing component,
  remove) must go through `it->stage`, never `it->world`. Reads use `it->world`. OnSet
  observers may set components directly.
- `ecs_ensure` in a system on a stage returns a temporary copy; use `ecs_get_mut` for
  existing components and keep registries in heap structs.
- Components without a ctor are not zero initialized. Register `.ctor = flecs_default_ctor`
  for every private component with flags.
- During deletion the OnRemove destination table is the empty root table, not NULL; test
  `ecs_table_get_type(it->other_table)->count == 0`.
- Never mutate entities while iterating `ecs_children`; collect them first.
- `ecs_os_malloc` needs `ecs_os_init()` (or a world) before use."""

RULE_TESTS = """Add every new case to `test/<project>/project.json` and its implementation to the
matching `<Suite>.c`. A test function starts with a single `// {path}` comment line and is
standalone: no test helpers, only callbacks may be shared. Run the suite you are changing
while you iterate and the whole project once at the end:

```sh
bake3 run test/<project> --local-env={env} -- <Suite> -j 8
bake3 run test/<project> --local-env={env} -- -j 8
```

Never edit an existing test to make your change pass; if a test looks wrong, report it."""

RULE_VISUAL = """`python3 tools/integration.py visual` renders every scene and compares it with
`test/integration/visual/baseline/<scene>.png`; it fails when the largest channel delta is
above 24 and more than 200 pixels differ by more than 16, so GPU speckle passes. Look only
at the scene of this task, refresh its baseline with `--update-baseline <scene>` when the
change is intended, and leave every other baseline matching. Do not open screenshots of
scenes you are not working on."""

RULE_COMMITS = """Commit each logical change on its own, with a single short subject line: no
body, no bullet points, no `Co-Authored-By` trailer. Stage explicit paths with
`git add <path>`, never `-A` or `.`, because other agents have their own changes in this
tree; retry the command when `index.lock` is taken. Never run `git push`."""

RULE_DELIVERABLE = """Produce the deliverable from the repository root:

```sh
python3 project/pm.py deliver {path} --tests <project>[:<Suite>] [--bench <project>] --env {env}
```

It writes `project/db/{path}/deliverable/tests.json` and `bench.json` and fails when a test
fails. Add a `deliverable/README.md` of under 20 lines saying what was built, what was
verified and what was left out, plus the screenshot of your own scene when there is one.
Close with `python3 project/pm.py done {path}`, which refuses while something is missing."""

RULE_ECONOMY = """Keep the run cheap: no browser checks, no screenshots other than the scene of
this task, and no full test sweep until the end. Read the files the task needs and stop
there, prefer a targeted suite over a project run, and keep the final report short."""

RULE_HEADLESS = """Never open a window. Every run of `box_app`, a bench or a scene is headless:

```sh
bake3 run box_app --local-env={env} -- --headless --timeout 30 --port 0 etc/scenes/default.flecs
```

To drive the application, keep the command port (`--port <n>`, one per agent) and send
commands over HTTP; to see what it renders, use the `capture` command or a scene that sets
`Surface.write_to_file`, and inspect the PNG. A headless run needs `--frames` or `--timeout`
and steps with a fixed delta, which is what makes captures deterministic. A windowed run
grabs the keyboard and the screen of the person working next to you; if a check truly needs
one, stop and report it instead."""

RULE_WORKTREE = """Another agent is editing this tree. Verify in a private worktree:
`git worktree add ../box-{env} HEAD`, symlink `.bake/bundles` into it and build there with
`--local-env={env}`, so a half written file of another agent cannot break your run. Commit
from the main tree with explicit paths."""

RULE_REPORT = """Report back in under {words} words: what you built, how you verified it (test
counts, lint, the scene) and anything the next task needs to know. No code, no file
listings, no restating of this prompt."""

BRIEF_RULES = (
    ("Build environment", RULE_ENV),
    ("File ownership", RULE_FILES),
    ("Style and linter", RULE_STYLE),
    ("Flecs pitfalls", RULE_FLECS),
    ("Tests", RULE_TESTS),
    ("Visual baselines", RULE_VISUAL),
    ("Commits", RULE_COMMITS),
    ("Deliverable", RULE_DELIVERABLE),
    ("Headless", RULE_HEADLESS),
    ("Economy", RULE_ECONOMY),
)


def load_epics():
    epics = []
    if os.path.isdir(DB):
        for name, full in child_dirs(DB):
            epics.append(walk_item(full, 0, name))
    return epics


def cmd_next(args):
    epics = load_epics()
    plan = derived_plan(epics, args.count)
    priority = {epic["name"]: epic["priority"] for epic in epics}
    if not plan["next_up"]:
        print("no ready unstarted task")
    for index, item in enumerate(plan["next_up"], 1):
        rank = priority.get(item["epic"], PRIORITY_DEFAULT)
        print(f"{index}. {item['path']}  [{item['epic']} priority {rank}]  {item['title']}")
        print(f"   depends on: {', '.join(item['dependencies']) or 'nothing'}")
    return 0


def item_text(path, *parts):
    return read_text(os.path.join(DB, *path.split("/"), *parts))


def quote_document(text):
    """Fold an embedded README one heading level down so the brief keeps one outline."""
    lines = (text or "TODO").strip().splitlines()
    return "\n".join("#" + line if line.startswith("#") else line for line in lines)


def dependency_sections(deps):
    blocks = []
    for dep in deps:
        dep = str(dep)
        level = "epic" if "/" not in dep else "task"
        text = item_text(dep, "deliverable", "README.md")
        if text:
            blocks.append(f"# Report of {level} `{dep}`\n\n{quote_document(text)}")
            continue
        spec = item_text(dep, "README.md")
        if spec:
            blocks.append(f"# Spec of {level} `{dep}` (no report yet)\n\n{quote_document(spec)}")
    return blocks


def brief_context(path, parts, args):
    files = [name.strip() for name in (args.files or "").split(",") if name.strip()]
    owned = ", ".join("`%s`" % name for name in files)
    return {
        "path": path,
        "repo": REPO,
        "epic": parts[0],
        "env": args.env or parts[-1],
        "files": owned or "the files this task names; the coordinator pinned no list",
        "words": REPORT_WORDS,
    }


def cmd_brief(args):
    directory, parts = item_dir(args.path)
    if len(parts) < 2:
        sys.exit(f"brief takes <epic>/<task>, not '{args.path}'")
    if not os.path.isdir(directory):
        sys.exit(f"{args.path} does not exist")
    progress = read_json(os.path.join(directory, "progress.json")) or {}
    context = brief_context(args.path, parts, args)
    blocks = [BRIEF_HEADER.format(**context)]
    blocks.append(f"# Task `{args.path}`\n\n{quote_document(item_text(args.path, 'README.md'))}")
    blocks.append(f"# Epic `{parts[0]}`\n\n{quote_document(item_text(parts[0], 'README.md'))}")
    blocks.extend(dependency_sections(progress.get("dependencies") or []))
    blocks.append("# Rules")
    for title, body in BRIEF_RULES:
        blocks.append(f"## {title}\n\n{body.format(**context).strip()}")
    if args.worktree:
        blocks.append(f"## Worktree\n\n{RULE_WORKTREE.format(**context).strip()}")
    blocks.append(RULE_REPORT.format(**context).strip())
    print("\n\n".join(blocks))
    print(f"launch {args.path} with model {args.model} in env {context['env']}", file=sys.stderr)
    return 0


def run_tool(cmd):
    return subprocess.run(cmd, cwd=REPO).returncode


def parse_test_specs(text):
    specs = []
    for part in (text or "").split(","):
        part = part.strip()
        if not part:
            continue
        project, _, suite = part.partition(":")
        if not re.fullmatch(r"[a-z0-9_]+", project):
            sys.exit(f"'{project}' is not a test project name")
        specs.append((project, suite.strip() or None))
    if not specs:
        sys.exit("--tests needs at least one <project>[:<suite>]")
    return specs


def report_names(specs):
    seen = {}
    for project, _ in specs:
        seen[project] = seen.get(project, 0) + 1
    names = []
    for project, suite in specs:
        if len(specs) == 1:
            names.append("tests.json")
        elif seen[project] > 1 and suite:
            names.append("tests_%s_%s.json" % (project, re.sub(r"[^a-z0-9]+", "_", suite.lower())))
        else:
            names.append(f"tests_{project}.json")
    return names


def merge_test_reports(reports):
    tests = []
    projects = []
    for data in reports:
        name = data.get("project") or "unknown"
        for case in data.get("tests") or []:
            row = dict(case)
            row["project"] = name
            tests.append(row)
        projects.append({"project": name, "pass": number(data.get("pass")), "fail": number(data.get("fail")),
                         "empty": number(data.get("empty")), "elapsed": number(data.get("elapsed"))})
    projects.sort(key=lambda row: row["project"])
    merged = {"project": "all", "timestamp": dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")}
    for key in TEST_STATUSES:
        merged[key] = sum(row[key] for row in projects)
    merged["elapsed"] = round(sum(row["elapsed"] for row in projects), 6)
    merged["tests"] = tests
    merged["projects"] = projects
    return merged


def run_tests(specs, out, env, failed):
    reports = []
    for (project, suite), name in zip(specs, report_names(specs)):
        report = os.path.join(out, name)
        if os.path.isfile(report):
            os.remove(report)
        cmd = ["bake3", "run", "test/" + project, f"--local-env={env}", "--"]
        if suite:
            cmd.append(suite)
        cmd += ["-j", str(TEST_JOBS), "--json", report]
        code = run_tool(cmd)
        data = read_json(report)
        label = "test/" + project + (":" + suite if suite else "")
        if not isinstance(data, dict) or not isinstance(data.get("tests"), list):
            print(f"FAIL {label}: no usable report at {name}")
            failed.append(label)
            continue
        reports.append(data)
        if code != 0 and not number(data.get("fail")):
            failed.append(label)
        print(f"{label}: {number(data.get('pass'))} passed, {number(data.get('fail'))} failed -> {name}")
    if len(reports) > 1:
        merged = merge_test_reports(reports)
        write_json(os.path.join(out, "tests.json"), merged)
        print(f"tests.json: {merged['pass']} passed, {merged['fail']} failed over {len(reports)} project(s)")
    return reports


def run_bench(names, out, env, failed):
    tmp = tempfile.mkdtemp()
    try:
        code = run_tool([sys.executable, os.path.join(REPO, "tools", "bench.py"), *names,
                         "--env", env, "--out", tmp])
        for name in names:
            source = os.path.join(tmp, name + ".json")
            target = "bench.json" if len(names) == 1 else f"bench_{name}.json"
            if not os.path.isfile(source):
                print(f"FAIL bench/{name}: no report")
                failed.append("bench/" + name)
                continue
            shutil.copyfile(source, os.path.join(out, target))
            print(f"bench/{name} -> {target}")
        if code != 0 and not failed:
            failed.append("tools/bench.py")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def cmd_deliver(args):
    directory, parts = item_dir(args.path)
    if not os.path.isdir(directory):
        sys.exit(f"{args.path} does not exist")
    out = os.path.join(directory, "deliverable")
    os.makedirs(out, exist_ok=True)
    env = args.env or parts[-1]
    failed = []
    reports = run_tests(parse_test_specs(args.tests), out, env, failed)
    if args.bench:
        run_bench([name.strip() for name in args.bench.split(",") if name.strip()], out, env, failed)
    passed = sum(number(data.get("pass")) for data in reports)
    broken = sum(number(data.get("fail")) for data in reports)
    print(f"{args.path}: {passed} passed, {broken} failed, {len(failed)} run(s) did not produce a report")
    return 1 if broken or failed else 0


def epic_readme_gaps(text):
    if text is None:
        return ["deliverable/README.md"]
    lines = text.splitlines()
    gaps = []
    if len(lines) > EPIC_README_LINES:
        gaps.append(f"deliverable/README.md is {len(lines)} lines, the limit is {EPIC_README_LINES}")
    found = [line[3:].strip() for line in lines if line.startswith("## ")]
    if [name for name in found if name in EPIC_SECTIONS] != list(EPIC_SECTIONS):
        gaps.append("deliverable/README.md sections " + ", ".join(EPIC_SECTIONS) + " in that order")
    return gaps


def top_level(item, predicate):
    return any("/" not in d["name"] and predicate(d["name"]) for d in item["deliverables"])


def deliverable_gaps(item, bench=True):
    """List what an item still misses before it may be marked complete."""
    if item["level"] == "subtask":
        return []
    out = os.path.join(DB, *item["path"].split("/"), "deliverable")
    gaps = []
    tests = read_json(os.path.join(out, "tests.json"))
    if not isinstance(tests, dict) or not isinstance(tests.get("tests"), list):
        gaps.append("deliverable/tests.json holding a 'tests' list")
    if item["level"] == "task":
        if bench and item["bench"] is not False and not top_level(item, is_bench_report):
            gaps.append("deliverable/bench.json, or \"bench\": false in progress.json")
        return gaps
    gaps.extend(epic_readme_gaps(read_text(os.path.join(out, "README.md"))))
    reports = os.path.join(out, "bench")
    if not os.path.isdir(reports) or not any(n.endswith(".json") for n in os.listdir(reports)):
        gaps.append("deliverable/bench/ with at least one report")
    if not os.path.isdir(os.path.join(out, "integration")):
        if not top_level(item, lambda name: name.endswith(".png")):
            gaps.append("deliverable/integration/ or screenshots")
    if not os.path.isfile(os.path.join(out, *EPIC_WASM)):
        gaps.append("deliverable/" + "/".join(EPIC_WASM))
    for task in item["children"]:
        if task["status"] not in DONE_STATUSES:
            gaps.append(f"task {task['path']} is still {task['status']}")
    return gaps


def strict_since():
    progress = read_json(os.path.join(DB, *STRICT_TASK.split("/"), "progress.json"))
    if not isinstance(progress, dict) or not progress.get("created"):
        return None
    return time_value(progress["created"])


def cmd_done(args):
    directory, parts = item_dir(args.path)
    if not os.path.isdir(directory):
        sys.exit(f"{args.path} does not exist")
    gaps = deliverable_gaps(walk_item(directory, len(parts) - 1, args.path))
    for gap in gaps:
        print(f"{args.path}: missing {gap}")
    if gaps and not args.force:
        print(f"{args.path} stays open: {len(gaps)} item(s) missing, pass --force to override")
        return 1
    if gaps:
        print(f"warning: {args.path} completed with --force while {len(gaps)} item(s) are missing")
    set_status(args.path, "complete")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    serve = sub.add_parser("serve")
    serve.add_argument("--port", type=int, default=8090)
    serve.set_defaults(fn=cmd_serve)

    new = sub.add_parser("new")
    new.add_argument("kind", choices=LEVELS)
    new.add_argument("path")
    new.add_argument("title")
    new.add_argument("--brief")
    new.add_argument("--type", choices=KINDS, default="feature")
    new.add_argument("--deps")
    new.add_argument("--priority", type=int, help="epic order, lower runs first (default %d)" % PRIORITY_DEFAULT)
    new.set_defaults(fn=cmd_new)

    for name, status in (("start", "in progress"), ("discard", "discarded"), ("reset", "pending")):
        p = sub.add_parser(name)
        p.add_argument("path")
        p.set_defaults(fn=lambda a, s=status: set_status(a.path, s))

    done = sub.add_parser("done")
    done.add_argument("path")
    done.add_argument("--force", action="store_true", help="complete the item although something is missing")
    done.set_defaults(fn=cmd_done)

    nxt = sub.add_parser("next")
    nxt.add_argument("count", nargs="?", type=int, default=NEXT_UP)
    nxt.set_defaults(fn=cmd_next)

    brief = sub.add_parser("brief")
    brief.add_argument("path")
    brief.add_argument("--files", help="comma separated paths the agent owns")
    brief.add_argument("--env", help="private build environment (default: the task name)")
    brief.add_argument("--model", choices=("opus", "sonnet"), default="sonnet")
    brief.add_argument("--worktree", action="store_true", help="another agent is editing the tree")
    brief.set_defaults(fn=cmd_brief)

    deliver = sub.add_parser("deliver")
    deliver.add_argument("path")
    deliver.add_argument("--tests", required=True, help="<project>[:<suite>][,<project>[:<suite>]]")
    deliver.add_argument("--bench", help="<project>[,<project>] passed to tools/bench.py")
    deliver.add_argument("--env", help="private build environment (default: the task name)")
    deliver.set_defaults(fn=cmd_deliver)

    sub.add_parser("status").set_defaults(fn=cmd_status)
    sub.add_parser("check").set_defaults(fn=cmd_check)

    args = parser.parse_args()
    rc = args.fn(args)
    sys.exit(rc or 0)


if __name__ == "__main__":
    main()
