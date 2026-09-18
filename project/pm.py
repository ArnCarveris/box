#!/usr/bin/env python3
"""Project management tool for box: walks project/db and serves the status page.

Usage:
  pm.py serve [--port 8090]
  pm.py new <kind> <path> "<title>" [--type feature|bug|refactor] [--deps a/b,c]
  pm.py start|done|discard|reset <path>
  pm.py status
  pm.py check
"""
import argparse
import datetime as dt
import http.server
import json
import mimetypes
import os
import re
import sys
import urllib.parse

ROOT = os.path.dirname(os.path.abspath(__file__))
DB = os.path.join(ROOT, "db")
STATUSES = ("pending", "in progress", "complete", "discarded")
KINDS = ("bug", "feature", "refactor")
LEVELS = ("epic", "task", "subtask")


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
        "deliverables": list_deliverables(path),
        "children": [],
    }
    if level + 1 < len(LEVELS):
        for name, full in child_dirs(path):
            item["children"].append(walk_item(full, level + 1, rel + "/" + name))
        item["children"].sort(key=lambda c: (c["created"] or "", c["name"]))
    return item


def collect_state():
    epics = []
    if os.path.isdir(DB):
        for name, full in child_dirs(DB):
            epics.append(walk_item(full, 0, name))
        epics.sort(key=lambda c: (c["created"] or "", c["name"]))
    return {"generated": now(), "epics": epics}


class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        route = urllib.parse.unquote(parsed.path)
        if route == "/" or route == "/index.html":
            return self.send_file(os.path.join(ROOT, "index.html"), "text/html; charset=utf-8")
        if route == "/api/state":
            body = json.dumps(collect_state()).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if route.startswith("/deliverable/"):
            rel = os.path.normpath(route[len("/deliverable/"):])
            full = os.path.join(DB, rel)
            if rel.startswith("..") or "/deliverable/" not in full + "/" or not os.path.isfile(full):
                return self.send_error(404)
            ctype = mimetypes.guess_type(full)[0] or "application/octet-stream"
            return self.send_file(full, ctype)
        return self.send_error(404)

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
    with open(os.path.join(directory, "progress.json"), "w") as f:
        json.dump(progress, f, indent=2)
        f.write("\n")


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
    write_progress(directory, {
        "kind": args.type,
        "status": "pending",
        "created": now(),
        "start_time": None,
        "end_time": None,
        "dependencies": deps,
    })
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


def cmd_check(args):
    problems = []
    known = set()

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
    for problem in problems:
        print(problem)
    print(f"{len(problems)} problem(s)")
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
