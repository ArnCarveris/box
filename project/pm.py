#!/usr/bin/env python3
"""Project management tool for box: walks project/db and serves the status page.

Usage:
  pm.py serve [--port 8090]
  pm.py new <kind> <path> "<title>" [--type feature|bug|refactor] [--deps a/b,c] [--priority 100]
  pm.py start|done|discard|reset <path>
  pm.py status
  pm.py check
"""
import argparse
import datetime as dt
import http.server
import json
import os
import re
import sys
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
BENCH_FIELDS = ("entities", "static", "dynamic", "frames")
MICRO_FIELDS = ("median_ns", "ci_low_ns", "ci_high_ns", "mean_ns", "stddev_ns", "min_ns", "max_ns",
                "p95_ns", "p99_ns", "outliers", "iterations", "samples", "items_per_sec")
BENCH_PROJECTS = os.path.join(REPO, "bench")
BENCH_EXEMPT_EPIC = "project_tool"
LOCKED_STATUSES = ("complete", "in progress")
DONE_STATUSES = ("complete", "discarded")
PRIORITY_DEFAULT = 100
UNRANKED = 1 << 30
NEXT_UP = 10


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


def epic_names():
    return [name for name, _ in child_dirs(DB)] if os.path.isdir(DB) else []


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


def derived_plan(epics):
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
    next_up = [item for item in order if item["path"] in first and not item["start_time"]][:NEXT_UP]
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
    return {
        "generated": now(),
        "reports": reports,
        "suites": suites,
        "suite_history": suite_history,
        "case_history": case_history,
        "projects": [projects[name] for name in sorted(projects)],
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
    projects = {}
    for key in sorted(latest):
        row = latest[key]
        project = projects.setdefault(row["project"], {
            "project": row["project"], "suites": 0, "cases": 0, "median_ns": 0.0,
            "time": row["time"], "item": row["item"], "url": row["url"],
        })
        project["suites"] += 1
        project["cases"] += row["cases"]
        project["median_ns"] = round(project["median_ns"] + row["median_ns"], 3)
        if time_value(row["time"]) >= time_value(project["time"]):
            project.update({"time": row["time"], "item": row["item"], "url": row["url"]})
    return {
        "micro_projects": [projects[name] for name in sorted(projects)],
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


def collect_state():
    epics = []
    if os.path.isdir(DB):
        for name, full in child_dirs(DB):
            epics.append(walk_item(full, 0, name))
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


def write_progress(directory, progress):
    path = os.path.join(directory, "progress.json")
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(progress, f, indent=2)
        f.write("\n")
    os.replace(tmp, path)


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

    for name, status in (("start", "in progress"), ("done", "complete"), ("discard", "discarded"), ("reset", "pending")):
        p = sub.add_parser(name)
        p.add_argument("path")
        p.set_defaults(fn=lambda a, s=status: set_status(a.path, s))

    sub.add_parser("status").set_defaults(fn=cmd_status)
    sub.add_parser("check").set_defaults(fn=cmd_check)

    args = parser.parse_args()
    rc = args.fn(args)
    sys.exit(rc or 0)


if __name__ == "__main__":
    main()
