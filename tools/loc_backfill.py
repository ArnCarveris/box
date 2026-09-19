#!/usr/bin/env python3
"""Backfills lines of code counts into existing bake3 build reports.

Usage:
  loc_backfill.py [--force]

Walks every project/db/*/deliverable/build_*.json, checks out the report's
git.sha into a temporary git worktree, runs the same cloc invocation bake3
runs during a build over the source and include folders of every project the
report touched, and writes the counts into the report in place: totals.loc
for the workspace and totals.project.<name>.loc for each project. A report
that already carries counts is left alone unless --force is given. A report
whose original build was dirty gets a "dirty": true field alongside its
counts, since the backfilled numbers come from a clean checkout of its sha
rather than the working tree the original build measured. Run once so every
existing report carries counts.
"""
import argparse
import glob
import json
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "project"))
import pm

DB = os.path.join(ROOT, "project", "db")
REPORT_GLOB = os.path.join(DB, "*", "deliverable", "build_*.json")
LOC_EXCLUDE_DIRS = ".bake,generated,deps,bundles"
LOC_FIELDS = ("files", "code", "comment", "blank")


def build_reports():
    """List every bake3 build report under project/db, sorted for stable output."""
    return sorted(glob.glob(REPORT_GLOB))


def has_counts(loc):
    """Tell a loc block with real counts from a missing one or a cloc-absent note."""
    return isinstance(loc, dict) and "code" in loc


def discover_project_paths(worktree):
    """Map every project id under a worktree to its directory, skipping .bake and .git."""
    paths = {}
    for dirpath, dirnames, filenames in os.walk(worktree):
        dirnames[:] = [d for d in dirnames if d not in (".bake", ".git")]
        if "project.json" not in filenames:
            continue

        data = pm.read_json(os.path.join(dirpath, "project.json"))
        if isinstance(data, dict) and isinstance(data.get("id"), str) and data["id"]:
            paths[data["id"]] = dirpath

    return paths


def run_cloc(paths):
    """Run cloc over a list of directories and return its counts, or None on failure.

    :param paths: existing directories to scan.
    :return: a loc block with files, code, comment, blank and by_language.
    """
    if not paths:
        return {"files": 0, "code": 0, "comment": 0, "blank": 0, "by_language": {}}

    cmd = ["cloc", "--json", "--quiet", "--exclude-dir=" + LOC_EXCLUDE_DIRS, *paths]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    except OSError:
        return None
    if proc.returncode != 0:
        return None

    try:
        data = json.loads(proc.stdout)
    except ValueError:
        return None
    if not isinstance(data, dict):
        return None

    totals = {"files": 0, "code": 0, "comment": 0, "blank": 0, "by_language": {}}
    for name, row in data.items():
        if name in ("header", "SUM") or not isinstance(row, dict):
            continue

        counts = {
            "files": int(pm.number(row.get("nFiles"))),
            "code": int(pm.number(row.get("code"))),
            "comment": int(pm.number(row.get("comment"))),
            "blank": int(pm.number(row.get("blank"))),
        }
        for field in LOC_FIELDS:
            totals[field] += counts[field]
        totals["by_language"][name] = counts

    return totals


def project_loc(project_path):
    """Run cloc over a project's src and include folders inside a worktree checkout."""
    dirs = [d for d in (os.path.join(project_path, "src"), os.path.join(project_path, "include"))
            if os.path.isdir(d)]

    return run_cloc(dirs)


def merge_loc(blocks):
    """Merge a list of loc blocks into one workspace total."""
    merged = {"files": 0, "code": 0, "comment": 0, "blank": 0, "by_language": {}}
    for block in blocks:
        for field in LOC_FIELDS:
            merged[field] += block[field]
        for language, counts in block["by_language"].items():
            dest = merged["by_language"].setdefault(
                language, {"files": 0, "code": 0, "comment": 0, "blank": 0})
            for field in LOC_FIELDS:
                dest[field] += counts[field]

    return merged


def backfill_report(path, worktree, force):
    """Backfill the counts of one build report; return a one line status."""
    data = pm.read_json(path)
    if not isinstance(data, dict) or not isinstance(data.get("totals"), dict):
        return f"skip {path}: not a build report"

    totals = data["totals"]
    if not force and has_counts(totals.get("loc")):
        return f"skip {path}: already has counts"

    git = data.get("git") if isinstance(data.get("git"), dict) else {}
    sha = git.get("sha")
    if not sha:
        return f"skip {path}: no git sha on the report"

    checkout = subprocess.run(["git", "-C", worktree, "checkout", "--force", "--detach", sha],
                              capture_output=True, text=True, check=False)
    if checkout.returncode != 0:
        return f"skip {path}: cannot check out {sha[:7]}: {checkout.stderr.strip()}"

    project_names = sorted((totals.get("project") or {}).keys())
    project_paths = discover_project_paths(worktree)
    dirty = bool(git.get("dirty"))
    blocks = []
    missing = []
    for name in project_names:
        project_path = project_paths.get(name)
        if not project_path:
            missing.append(name)
            continue

        block = project_loc(project_path)
        if block is None:
            missing.append(name)
            continue

        if dirty:
            block["dirty"] = True

        totals["project"][name]["loc"] = block
        blocks.append(block)

    if not blocks:
        return f"skip {path}: no project resolved at {sha[:7]}"

    workspace = merge_loc(blocks)
    if dirty:
        workspace["dirty"] = True
    totals["loc"] = workspace
    pm.write_json(path, data)

    detail = f"{workspace['files']} file(s), {workspace['code']} code line(s)"
    if missing:
        detail += "; missing " + ", ".join(missing)

    return f"done {path} @ {sha[:7]}: {detail}"


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--force", action="store_true",
                        help="recompute counts even for a report that already has them")
    args = parser.parse_args()

    if not shutil.which("cloc"):
        sys.exit("cloc is not on PATH")

    reports = build_reports()
    if not reports:
        print("no build reports under project/db")
        return 0

    base = tempfile.mkdtemp(prefix="box_loc_backfill_")
    worktree = os.path.join(base, "wt")
    add = subprocess.run(["git", "worktree", "add", "--detach", worktree, "HEAD"],
                         cwd=ROOT, capture_output=True, text=True, check=False)
    if add.returncode != 0:
        sys.exit(f"failed to create a worktree: {add.stderr.strip()}")

    done = 0
    try:
        for path in reports:
            line = backfill_report(path, worktree, args.force)
            print(line, flush=True)
            if line.startswith("done "):
                done += 1
    finally:
        subprocess.run(["git", "worktree", "remove", "--force", worktree],
                       cwd=ROOT, capture_output=True, text=True, check=False)
        shutil.rmtree(base, ignore_errors=True)

    print(f"backfilled {done} of {len(reports)} report(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
