#!/usr/bin/env python3
"""Runs the integration scenes of box.

Usage:
  integration.py visual [--dir dir] [--app project] [--env name] [--cfg mode] [--out dir]
                        [--frames n] [--size WxH] [--update-baseline scene] [--json file]
                        [scene ...]

visual renders every <dir>/<scene>.flecs (test/integration/visual by default) headless with
boxrun, a static camera and a fixed delta, and compares the last frame with
<dir>/baseline/<scene>.png. A scene fails when the difference is strong and widespread: the
largest channel delta is above 24 and more than 200 pixels have a channel off by more than 16,
so GPU speckle and faint drift pass. A missing baseline fails too.

--update-baseline <scene> renders that scene and stores the render as its baseline; repeat it
for more scenes. Scenes are names or paths; without any, every scene of the folder runs.

Every run writes <out>/index.html (test/integration/out by default) with the baseline, the
render and a difference image of every scene, failures first. The difference is green where the
render is brighter, red where it is darker, yellow where only the hue moved, at half strength
when no channel moved more than 16, over a dimmed grey copy of the baseline where the pixels are
identical. --json writes the results in the bake3 test report format.

The run builds the app once (bake3 build) and renders every scene with a copy of that binary in
.bake/integration, removed when the run ends, so a build in the same environment during the run
cannot change the binary halfway. When the load guard refuses a render (load average above the
core count), the run waits for the load to drop, printing a line when it starts waiting and every
minute, and tries again, up to --load-wait seconds for the whole run; a scene still refused then
fails as "not rendered: cpu oversubscribed", never as a render difference. When a commit that
changes etc, src, include, apps or test/integration lands in the tree during the run, the run
says so. Every run is also kept in test/integration/out/history (--history moves it):
<run>/results.json with every scene's diff stats, exit code, attempts, refusals, load wait, load
before, at the render and after, render seconds, the error lines of its log and the warnings of
headless readiness waits that timed out (readiness, also appended to a failing scene's detail),
plus the render, baseline, diff, log and script of every failing scene, and one summary line per
run in runs.jsonl. The last 30 runs are kept.

  integration.py perf [--dir dir] [--app project] [--env name] [--cfg mode] [--out dir]
                      [--size WxH] [--seconds s] [--cpu-budget pct] [--stall ms] [--max-load n]
                      [--json file] [scene ...]

perf runs every <dir>/<scene>.flecs (test/integration/perf by default) headless with boxrun on an
offscreen surface of --size, unless the scene declares its own Surface. The scene sets the
box.perf.PerfRun singleton (30, 60, 90 or 120 FPS) and runs in a release build (--cfg overrides
it); the engine paces every frame to the target (or presents to a simulated vsync display of
PerfRun.refresh Hz that paces it), orbits the camera
around the bounds of the shapes (or lets its controller fly it) and counts as errors every frame
interval outside [0.9, 1.1] target intervals, every delta_time that differs more than 1 ms from the
wall time since the previous frame or from the time the display showed the frame before, and every
present that missed its refresh. A target that does not divide the refresh rate (90 FPS on a
120 Hz display) shows frames in a cadence of 1 and 2 refreshes: only the display delta and
the missed presents count then. A scene passes only with zero errors, when a view renders the
orbiting (or flying) camera, when an orbit covers at least one revolution (frames >= fps *
period) or a flying camera moved, and when the warmup, the only frames not measured, is at most one second. A run is retried
only when the load guard refused to start it; a run that started is never retried. Each result
also holds the range of the visible, frustum culled and occlusion culled instances of the measured
views and the process CPU use per frame. The results go to <out>/perf.json (test/integration/out
by default), which the pm page charts, with the 1 minute load average before and after every
scene. --seconds measures that many seconds at every scene's target instead of its frames.
--stall adds busy milliseconds to every frame to prove that the checks fail a slow scene.
--max-load waits, up to --load-wait seconds, until the load average is at most n before every
scene. Like visual, perf runs every scene with one copy of the app built at the start and waits
out load guard refusals, up to --guard-wait seconds for the whole run.
"""
import argparse
import glob
import html
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import time
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VISUAL_DIR = os.path.join(ROOT, "test", "integration", "visual")
PERF_DIR = os.path.join(ROOT, "test", "integration", "perf")
OUT_DIR = os.path.join(ROOT, "test", "integration", "out")
DEFAULT_APP = "apps/boxrun"
MAX_DELTA = 24
PIXEL_DELTA = 16
MAX_PIXELS = 200
RENDER_TIMEOUT = 120
GUARD_WAIT = 10
GUARD_PAUSE = 2
GUARD_REPORT = 60
SNAPSHOT_DIR = os.path.join(ROOT, ".bake", "integration")
TREE_PATHS = ("etc", "src", "include", "apps", "test/integration")
TREE_LIST = 10
LOAD_WAIT = 1800
PERF_TIMEOUT = 240
PERF_FIELDS = ("target_fps", "frames", "errors", "slow", "fast", "min_ms", "mean_ms", "p50_ms",
               "p99_ms", "max_ms", "fps", "work_mean_ms", "work_p99_ms", "work_max_ms",
               "slept_ms", "stalls", "stall_ms", "warmup", "period", "revolutions", "views",
               "delta_errors", "delta_tolerance_ms", "delta_max_ms", "display_delta_errors",
               "present_errors", "refresh", "cadence",
               "present_mean_ms", "cpu_percent", "cpu_ms", "fly", "travelled")
CULL_FIELDS = ("visible", "frustum_culled", "occlusion_culled")
PERF_KEYS = PERF_FIELDS + ("drawn", "culled", "deg_per_frame", "cull_samples", "cull_changes",
                           "load_before", "load_after", "attempts", "refusals", "load_wait",
                           "load_run") + tuple(
    "%s_%s" % (name, end) for name in CULL_FIELDS for end in ("min", "max"))
MAX_SETTLE_SECONDS = 1.0
HISTORY_RUNS = 30
VISUAL_KEYS = ("max", "mean", "px", "frames", "run_seconds", "exit", "attempts", "refusals",
               "load_wait", "load_before", "load_run", "load_after", "readiness")
HISTORY_FILES = (".png", "_baseline.png", "_diff.png", ".log", ".run.flecs")
LOG_NOTE = re.compile(r"error|warning|timed out|lost|failed|abort", re.IGNORECASE)
LOG_NOTES = 20
ANSI = re.compile(r"\x1b\[[0-9;]*m")
READINESS = re.compile(r"headless \w+ wait timed out")
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
PNG_CHANNELS = {0: 1, 2: 3, 4: 2, 6: 4}


def unfilter_add(a, b):
    n = len(a)
    high = int.from_bytes(b"\x80" * n, "big")
    low = int.from_bytes(b"\x7f" * n, "big")
    x = int.from_bytes(a, "big")
    y = int.from_bytes(b, "big")
    return bytearray((((x & low) + (y & low)) ^ ((x ^ y) & high)).to_bytes(n, "big"))


def unfilter_row(kind, line, prev, bpp):
    n = len(line)
    if kind == 1:
        for x in range(bpp, n):
            line[x] = (line[x] + line[x - bpp]) & 255
    elif kind == 2:
        line = unfilter_add(line, prev)
    elif kind == 3:
        for x in range(n):
            left = line[x - bpp] if x >= bpp else 0
            line[x] = (line[x] + ((left + prev[x]) >> 1)) & 255
    elif kind == 4:
        for x in range(n):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            pa = abs(b - c)
            pb = abs(a - c)
            pc = abs(a + b - c - c)
            if pa <= pb and pa <= pc:
                pred = a
            elif pb <= pc:
                pred = b
            else:
                pred = c
            line[x] = (line[x] + pred) & 255
    elif kind != 0:
        raise ValueError("unknown PNG filter %d" % kind)
    return line


def read_png(path):
    with open(path, "rb") as f:
        data = f.read()
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError("%s is not a PNG" % path)
    pos = len(PNG_SIGNATURE)
    idat = []
    width = height = channels = 0
    while pos + 8 <= len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or color not in PNG_CHANNELS or interlace:
                raise ValueError("%s: only 8 bit, non interlaced gray or RGB(A) PNGs" % path)
            channels = PNG_CHANNELS[color]
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
        pos += 12 + length
    raw = zlib.decompress(b"".join(idat))
    stride = width * channels
    prev = bytearray(stride)
    rows = []
    for y in range(height):
        offset = y * (stride + 1)
        line = unfilter_row(raw[offset], bytearray(raw[offset + 1:offset + 1 + stride]), prev,
                            channels)
        rows.append(to_rgb(line, channels))
        prev = line
    return width, height, rows


def to_rgb(line, channels):
    if channels == 3:
        return bytes(line)
    out = bytearray(len(line) // channels * 3)
    if channels == 4:
        out[0::3] = line[0::4]
        out[1::3] = line[1::4]
        out[2::3] = line[2::4]
    else:
        gray = line[0::channels]
        out[0::3] = gray
        out[1::3] = gray
        out[2::3] = gray
    return bytes(out)


def write_png(path, width, height, rows):
    def chunk(kind, body):
        return (struct.pack(">I", len(body)) + kind + body +
                struct.pack(">I", zlib.crc32(kind + body) & 0xffffffff))
    raw = b"".join(b"\x00" + bytes(row) for row in rows)
    with open(path, "wb") as f:
        f.write(PNG_SIGNATURE)
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        f.write(chunk(b"IEND", b""))


def compare(base, actual):
    bw, bh, brows = base
    aw, ah, arows = actual
    if (bw, bh) != (aw, ah):
        return {"ok": False, "reason": "size %dx%d, baseline %dx%d" % (aw, ah, bw, bh)}
    max_delta = 0
    total = 0
    pixels = 0
    for brow, arow in zip(brows, arows):
        if brow == arow:
            continue
        for x in range(0, len(brow), 3):
            d = max(abs(brow[x] - arow[x]), abs(brow[x + 1] - arow[x + 1]),
                    abs(brow[x + 2] - arow[x + 2]))
            if d:
                total += abs(brow[x] - arow[x]) + abs(brow[x + 1] - arow[x + 1]) + \
                    abs(brow[x + 2] - arow[x + 2])
                max_delta = max(max_delta, d)
                pixels += d > PIXEL_DELTA
    mean = total / float(bw * bh * 3) if bw and bh else 0.0
    ok = not (max_delta > MAX_DELTA and pixels > MAX_PIXELS)
    return {"ok": ok, "max": max_delta, "mean": round(mean, 4), "px": pixels}


def diff_image(base, actual):
    width, height, brows = base
    arows = actual[2]
    rows = []
    for brow, arow in zip(brows, arows):
        out = bytearray(len(brow))
        for x in range(0, len(brow), 3):
            br, bg, bb = brow[x], brow[x + 1], brow[x + 2]
            ar, ag, ab = arow[x], arow[x + 1], arow[x + 2]
            lb = br * 30 + bg * 59 + bb * 11
            if br == ar and bg == ag and bb == ab:
                g = lb // 100 // 4
                out[x] = out[x + 1] = out[x + 2] = g
                continue
            la = ar * 30 + ag * 59 + ab * 11
            d = max(abs(br - ar), abs(bg - ag), abs(bb - ab))
            s = 255 if d > PIXEL_DELTA else 128
            if la > lb:
                out[x + 1] = s
            elif la < lb:
                out[x] = s
            else:
                out[x] = out[x + 1] = s
        rows.append(out)
    return width, height, rows


def display(path):
    path = os.path.abspath(path)
    if path == ROOT or path.startswith(ROOT + os.sep):
        return os.path.relpath(path, ROOT)
    return path


def scene_name(path):
    return os.path.splitext(os.path.basename(path))[0]


def resolve_scene(directory, scene):
    if os.path.isfile(scene):
        return os.path.abspath(scene)
    path = os.path.join(directory, scene_name(scene) + ".flecs")
    if not os.path.isfile(path):
        sys.exit("no scene %s" % path)
    return path


def app_project(app):
    if os.path.isdir(os.path.join(ROOT, app)):
        return app
    if os.path.isdir(os.path.join(ROOT, "apps", app)):
        return "apps/" + app
    return app


def guard_budget(args):
    return "%d of %d s" % (round(args.guard_wait - args.load_budget), round(args.guard_wait))


def wait_for_cores(args):
    waited = 0
    cores = os.cpu_count() or 1
    load = load_average()
    if load is not None and load > cores and args.load_budget > 0:
        print("    waiting for the load average %.1f to drop to %d cores (guard wait %s used)"
              % (load, cores, guard_budget(args)), flush=True)
    while load is not None and load > cores and args.load_budget > 0:
        pause = min(GUARD_WAIT, args.load_budget)
        time.sleep(pause)
        args.load_budget -= pause
        waited += pause
        load = load_average()
        if waited % GUARD_REPORT < GUARD_WAIT and load is not None and load > cores:
            print("    still waiting: load %.1f on %d cores after %d s (guard wait %s used)"
                  % (load, cores, waited, guard_budget(args)), flush=True)
    return round(waited, 3)


def partial_output(data):
    if isinstance(data, bytes):
        return data.decode(errors="replace")

    return data or ""


def run_guarded(cmd, args, output, timeout, refused):
    log = ""
    code = -1
    run = {"attempts": 0, "timed_out": False, "refusals": 0, "load_wait": 0}
    while True:
        run["load_wait"] += wait_for_cores(args)
        if os.path.exists(output):
            os.unlink(output)
        run["attempts"] += 1
        run["load_run"] = load_average()
        start = time.time()
        try:
            proc = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True,
                                  timeout=timeout)
            log = proc.stdout + proc.stderr
            code = proc.returncode
        except subprocess.TimeoutExpired as e:
            log = "run timed out\n" + partial_output(e.stdout) + partial_output(e.stderr)
            code = -1
            run["timed_out"] = True
        run["run_seconds"] = round(time.time() - start, 3)
        if not refused(log):
            break
        run["refusals"] += 1
        if args.load_budget <= 0:
            break
        pause = min(GUARD_PAUSE, args.load_budget)
        time.sleep(pause)
        args.load_budget -= pause
        run["load_wait"] += pause
    run["refused"] = refused(log)
    run["exit"] = code
    lines = [ANSI.sub("", line).strip() for line in log.splitlines()]
    run["log_notes"] = [line for line in lines if LOG_NOTE.search(line)][:LOG_NOTES]
    run["readiness"] = [line for line in lines if READINESS.search(line)][:LOG_NOTES]
    return code, log, run


def refusal_detail(run, args):
    return ("not rendered: cpu oversubscribed (load %s on %d cores) after this scene waited %d s "
            "and the run %s of its guard wait" % (
                load_average(), os.cpu_count() or 1, run["load_wait"], guard_budget(args)))


def app_binary(args):
    project = app_project(args.app)
    with open(os.path.join(ROOT, project, "project.json")) as f:
        name = json.load(f)["id"].split(".")[-1]
    env = os.path.join(ROOT, ".bake", "local_env", *([args.env] if args.env else []))
    found = glob.glob(os.path.join(env, "build", name, "*-%s" % (args.cfg or "debug"), name))
    return max(found, key=os.path.getmtime) if found else None


def prepare_app(args):
    env = "--local-env=" + args.env if args.env else "--local-env"
    cmd = ["bake3", "build", app_project(args.app), env]
    if args.cfg:
        cmd += ["--cfg", args.cfg]
    proc = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True)
    if proc.returncode:
        sys.exit("building %s failed:\n%s" % (args.app, (proc.stdout + proc.stderr)[-4000:]))
    binary = app_binary(args)
    if not binary:
        sys.exit("built %s but found no binary of it in the bake3 environment" % args.app)
    os.makedirs(SNAPSHOT_DIR, exist_ok=True)
    snapshot = os.path.join(SNAPSHOT_DIR, "%s-%d" % (os.path.basename(binary), os.getpid()))
    shutil.copyfile(binary, snapshot)
    os.chmod(snapshot, 0o755)
    return snapshot, {"binary": display(binary),
                      "built": time.strftime("%Y-%m-%dT%H:%M:%SZ",
                                             time.gmtime(os.path.getmtime(binary))),
                      "size": os.path.getsize(binary)}


def render_scene(scene, png, args):
    width, height = (int(v) for v in args.size.lower().split("x"))
    with open(scene) as f:
        source = f.read()
    harness = os.path.join(os.path.dirname(png), scene_name(scene) + ".run.flecs")
    with open(harness, "w") as f:
        f.write("%s\n\nvisual_capture {\n  box.surface.Surface: {width: %d, height: %d, "
                "write_to_file: \"%s\"}\n}\n" % (source.rstrip(), width, height, png))
    cmd = [args.binary, "--headless", "--frames", str(args.frames), "--timeout",
           str(RENDER_TIMEOUT), "--port", "0", harness]
    code, log, run = run_guarded(cmd, args, png, RENDER_TIMEOUT * 2,
                                 lambda log: "cpu oversubscribed" in log)
    with open(os.path.join(os.path.dirname(png), scene_name(scene) + ".log"), "w") as f:
        f.write(log)
    return code == 0 and os.path.isfile(png), log, run


def run_scene(scene, baseline_dir, out, update, args):
    name = scene_name(scene)
    png = os.path.join(out, name + ".png")
    baseline = os.path.join(baseline_dir, name + ".png")
    report_base = os.path.join(out, name + "_baseline.png")
    report_diff = os.path.join(out, name + "_diff.png")
    for stale in (report_base, report_diff):
        if os.path.exists(stale):
            os.unlink(stale)
    start = time.time()
    load_before = load_average()
    rendered, log, run = render_scene(scene, png, args)
    result = {"scene": name, "path": display(scene), "status": "fail",
              "image": rendered, "baseline": False, "diff": False,
              "frames": args.frames, "load_before": load_before, "load_after": load_average()}
    result.update(run)
    if not rendered:
        result["detail"] = "render failed, see %s.log" % name
        if run.get("refused"):
            result["detail"] = refusal_detail(run, args)
        tail = log.strip().splitlines()[-15:]
        print("\n".join("    " + line for line in tail))
    elif update:
        os.makedirs(baseline_dir, exist_ok=True)
        existed = os.path.isfile(baseline)
        shutil.copyfile(png, baseline)
        result["status"] = "pass"
        result["detail"] = "baseline updated" if existed else "baseline created"
    elif not os.path.isfile(baseline):
        result["detail"] = "no baseline, run --update-baseline %s" % name
    if rendered and os.path.isfile(baseline):
        shutil.copyfile(baseline, report_base)
        result["baseline"] = True
        base = read_png(baseline)
        actual = read_png(png)
        cmp = compare(base, actual)
        if "reason" in cmp:
            if not update:
                result["detail"] = cmp["reason"]
        else:
            result.update({"max": cmp["max"], "mean": cmp["mean"], "px": cmp["px"]})
            write_png(report_diff, *diff_image(base, actual))
            result["diff"] = True
            if not update:
                result["status"] = "pass" if cmp["ok"] else "fail"
                result["detail"] = "max %d, mean %.4f, %d px > %d" % (
                    cmp["max"], cmp["mean"], cmp["px"], PIXEL_DELTA)
    if result["status"] != "pass" and result.get("readiness"):
        result["detail"] = "%s; %s" % (result.get("detail", ""), "; ".join(result["readiness"]))
    result["elapsed"] = round(time.time() - start, 6)
    return result


def image_cell(out, name):
    if not os.path.isfile(os.path.join(out, name)):
        return '<td><span class="none">&mdash;</span></td>'
    ref = html.escape(name)
    return '<td><a href="%s" target="_blank"><img src="%s" loading="lazy" alt="%s"></a></td>' % (
        ref, ref, ref)


def write_report(results, out, baseline_dir):
    failed = [r for r in results if r["status"] != "pass"]
    rows = []
    for r in sorted(results, key=lambda r: (r["status"] == "pass", r["scene"])):
        name = r["scene"]
        state = "pass" if r["status"] == "pass" else "fail"
        rows.append(
            '<tr class="%s"><td class="name">%s<br><span class="state %s">%s</span><br>'
            '<span class="detail">%s</span></td>%s%s%s</tr>' % (
                state, html.escape(name), state, state.upper(), html.escape(r.get("detail", "")),
                image_cell(out, name + "_baseline.png"), image_cell(out, name + ".png"),
                image_cell(out, name + "_diff.png")))
    page = """<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Visual scenes</title>
<style>
:root { --bg: #1a1a1a; --head: #222; --line: #333; --text: #eee; --muted: #aaa;
        --pass: #8f8; --fail: #f88; }
body { font-family: -apple-system, system-ui, sans-serif; background: var(--bg);
       color: var(--text); margin: 20px; }
h1 { font-weight: 400; margin-bottom: 4px; }
p { color: var(--muted); margin: 6px 0; }
table { border-collapse: collapse; width: 100%%; margin-top: 12px; }
th, td { padding: 10px; border-bottom: 1px solid var(--line); vertical-align: top; }
th { text-align: left; background: var(--head); font-weight: 600; position: sticky; top: 0; }
img { max-width: 480px; width: 100%%; display: block; background: #000; }
.name { font-weight: 600; min-width: 160px; }
.state.pass { color: var(--pass); } .state.fail { color: var(--fail); }
.detail { color: var(--muted); font-size: 0.9em; font-weight: 400; }
.none { color: #555; font-style: italic; }
.scroll { overflow-x: auto; }
body.failures tr.pass { display: none; }
</style></head><body>
<h1>Visual scenes &mdash; %d failed of %d</h1>
<p>Baselines: %s. A scene fails when the largest channel delta is above %d and more than %d
pixels differ by more than %d.</p>
<p>Diff: green brighter, red darker, yellow hue only, faint under a delta of %d, grey
identical.</p>
<p><label><input type="checkbox" onchange="document.body.classList.toggle('failures',
this.checked)"> Only failures</label></p>
<div class="scroll"><table>
<tr><th>Scene</th><th>Baseline</th><th>Current</th><th>Diff</th></tr>
%s
</table></div></body></html>
""" % (len(failed), len(results), html.escape(display(baseline_dir)), MAX_DELTA,
       MAX_PIXELS, PIXEL_DELTA, PIXEL_DELTA, "\n".join(rows))
    path = os.path.join(out, "index.html")
    with open(path, "w") as f:
        f.write(page)
    return path


def write_json(path, results, project, elapsed, suite="Visual", keys=("max", "mean", "px")):
    tests = []
    for r in results:
        row = {"suite": suite, "case": r["scene"], "status": r["status"],
               "elapsed": r["elapsed"], "detail": r.get("detail", "")}
        for key in keys:
            if key in r:
                row[key] = r[key]
        tests.append(row)
    report = {
        "project": project,
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "pass": sum(1 for r in results if r["status"] == "pass"),
        "fail": sum(1 for r in results if r["status"] != "pass"),
        "empty": 0,
        "timeout": 0,
        "elapsed": round(elapsed, 6),
        "tests": tests,
    }
    directory = os.path.dirname(os.path.abspath(path))
    os.makedirs(directory, exist_ok=True)
    with open(path, "w") as f:
        json.dump(report, f, indent=2)
        f.write("\n")


def default_out(directory):
    if os.path.abspath(directory) == VISUAL_DIR:
        return OUT_DIR
    rel = display(directory).strip(os.sep)
    return os.path.join(OUT_DIR, rel.replace(os.sep, "_").replace(".", "_"))


def ignore_out(out):
    if not os.path.abspath(out).startswith(OUT_DIR):
        return
    os.makedirs(OUT_DIR, exist_ok=True)
    marker = os.path.join(OUT_DIR, ".gitignore")
    if not os.path.isfile(marker):
        with open(marker, "w") as f:
            f.write("*\n")


def git_commit():
    try:
        proc = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, text=True,
                              capture_output=True, timeout=10)
        return proc.stdout.strip() or None
    except (OSError, subprocess.TimeoutExpired):
        return None


def tree_changes(commit):
    end = git_commit()
    if not commit or not end or end == commit:
        return []
    try:
        proc = subprocess.run(["git", "diff", "--name-only", commit, end, "--"] + list(TREE_PATHS),
                              cwd=ROOT, text=True, capture_output=True, timeout=30)
        return [line for line in proc.stdout.splitlines() if line.strip()]
    except (OSError, subprocess.TimeoutExpired):
        return []


def prune_history(history):
    runs = sorted(d for d in os.listdir(history) if os.path.isdir(os.path.join(history, d)))
    for stale in runs[:-HISTORY_RUNS]:
        shutil.rmtree(os.path.join(history, stale), ignore_errors=True)


def write_history(results, out, directory, args, started, elapsed, app, commit, changed):
    history = os.path.abspath(args.history) if args.history else os.path.join(OUT_DIR,
                                                                               "history")
    run_id = "%s-%d" % (time.strftime("%Y%m%dT%H%M%SZ", time.gmtime(started)), os.getpid())
    folder = os.path.join(history, run_id)
    ignore_out(history)
    os.makedirs(folder, exist_ok=True)
    failed = [r for r in results if r["status"] != "pass"]
    for r in failed:
        for suffix in HISTORY_FILES:
            source = os.path.join(out, r["scene"] + suffix)
            if os.path.isfile(source):
                shutil.copyfile(source, os.path.join(folder, r["scene"] + suffix))
    summary = {
        "run": run_id,
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(started)),
        "commit": commit,
        "commit_end": git_commit(),
        "tree_changed": changed,
        "dir": display(directory),
        "app": args.app,
        "env": args.env,
        "cfg": args.cfg,
        "frames": args.frames,
        "size": args.size,
        "cores": os.cpu_count(),
        "app_build": app,
        "elapsed": round(elapsed, 3),
        "passed": len(results) - len(failed),
        "failed": [{"scene": r["scene"], "detail": r.get("detail", ""),
                    "load_before": r.get("load_before"), "load_run": r.get("load_run"),
                    "readiness": r.get("readiness", [])}
                   for r in failed],
    }
    with open(os.path.join(folder, "results.json"), "w") as f:
        json.dump(dict(summary, results=results), f, indent=2)
        f.write("\n")
    with open(os.path.join(history, "runs.jsonl"), "a") as f:
        f.write(json.dumps(summary) + "\n")
    prune_history(history)
    return folder


def cmd_visual(args):
    directory = os.path.abspath(args.dir) if args.dir else VISUAL_DIR
    baseline_dir = os.path.join(directory, "baseline")
    out = os.path.abspath(args.out) if args.out else default_out(directory)
    ignore_out(out)
    os.makedirs(out, exist_ok=True)
    updates = {scene_name(resolve_scene(directory, s)) for s in args.update_baseline or []}
    names = args.scenes or sorted(args.update_baseline or []) or None
    if names:
        scenes = [resolve_scene(directory, s) for s in names]
    else:
        scenes = sorted(glob.glob(os.path.join(directory, "*.flecs")))
    if not scenes:
        sys.exit("no scenes in %s" % directory)
    start = time.time()
    commit = git_commit()
    args.binary, app = prepare_app(args)
    args.load_budget = args.guard_wait
    results = []
    try:
        for scene in scenes:
            r = run_scene(scene, baseline_dir, out, scene_name(scene) in updates, args)
            results.append(r)
            print("%s %s (%s)" % ("PASS" if r["status"] == "pass" else "FAIL", r["path"],
                                  r.get("detail", "")), flush=True)
    finally:
        os.unlink(args.binary)
    changed = tree_changes(commit)
    if changed:
        print("the tree changed during the run (%s to %s): %s" % (
            commit, git_commit(), ", ".join(changed[:TREE_LIST])))
    report = write_report(results, out, baseline_dir)
    failed = sum(1 for r in results if r["status"] != "pass")
    history = write_history(results, out, directory, args, start, time.time() - start, app,
                            commit, changed)
    print("history %s" % display(history))
    print("%d passed, %d failed, report %s" % (len(results) - failed, failed,
                                               display(report)))
    if args.json:
        write_json(args.json, results, display(directory), time.time() - start,
                   keys=VISUAL_KEYS)

    return 1 if failed else 0


def load_average():
    try:
        return round(os.getloadavg()[0], 2)
    except OSError:
        return None


def wait_for_load(limit, wait):
    load = load_average()
    deadline = time.time() + wait
    while limit is not None and load is not None and load > limit and time.time() < deadline:
        time.sleep(5)
        load = load_average()
    return load


def perf_script(scene, report, args):
    width, height = (int(v) for v in args.size.lower().split("x"))
    with open(scene) as f:
        source = f.read()
    extra = ""
    if args.stall:
        extra += ", stall: %g" % args.stall
    fps = re.search(r"PerfRun:\s*\{[^}]*\bfps:\s*(\d+)", source)
    if args.seconds and fps:
        extra += ", frames: %d" % round(int(fps.group(1)) * args.seconds)
    surface = ""
    if not re.search(r"\bSurface:", source):
        surface = "perf_surface {\n  box.surface.Surface: {width: %d, height: %d}\n}\n\n" % (
            width, height)
    return "%s\n\n%s$ {\n  box.perf.PerfRun: {quit: true, report: \"%s\"%s}\n}\n" % (
        source.rstrip(), surface, report, extra)


def perf_command(harness, args):
    cmd = [args.binary, "--headless", "--timeout", str(PERF_TIMEOUT), "--port", "0"]
    if args.cpu_budget is not None:
        cmd += ["--cpu-budget", str(args.cpu_budget)]
    return cmd + [harness]


def perf_cull(data, result):
    cull = data.get("cull") or {}
    result["cull_samples"] = cull.get("samples", 0)
    result["cull_changes"] = cull.get("changes", 0)
    for name in CULL_FIELDS:
        rng = cull.get(name) or {}
        result[name + "_min"] = rng.get("min", 0)
        result[name + "_max"] = rng.get("max", 0)
    visible = (cull.get("visible") or {}).get("mean", 0)
    hidden = sum((cull.get(name) or {}).get("mean", 0) for name in CULL_FIELDS[1:])
    result["drawn"] = round(visible)
    result["culled"] = round(hidden)


def perf_problems(data):
    problems = []
    fps = data.get("target_fps") or 0
    period = data.get("period") or 0
    fly = data.get("fly")
    if not data.get("views"):
        problems.append("no view renders the %s camera" % ("flying" if fly else "orbiting"))
    cull = data.get("cull") or {}
    if cull.get("samples") and not (cull.get("visible") or {}).get("max"):
        problems.append("the measured views draw nothing (beyond the far plane?)")
    if fly and not data.get("travelled"):
        problems.append("the flying camera did not move")
    if not fly and period <= 0:
        problems.append("the camera does not orbit (period %g)" % period)
    if not fly and fps and period and data.get("frames", 0) < fps * period:
        problems.append("the measurement covers %.2f of a revolution, frames must be at least "
                        "fps * period (%d)" % (data.get("frames", 0) / (fps * period),
                                               round(fps * period)))
    if fps and data.get("warmup", 0) > fps * MAX_SETTLE_SECONDS:
        problems.append("warmup %d frames exceeds the %g s settle period" % (
            data.get("warmup", 0), MAX_SETTLE_SECONDS))
    return problems


def perf_detail(data):
    refresh = data.get("refresh") or 0
    display = (", %g Hz display, present mean %.2f ms" % (refresh, data.get("present_mean_ms", 0))
               if refresh else "")
    if data.get("cadence"):
        display += (", cadence: only display delta and missed presents count, slow, fast and "
                    "delta reported")
    return ("%d errors (%d slow, %d fast, %d delta, %d display delta, %d present) in %d frames at "
            "%d fps%s, frame "
            "mean %.2f ms, p99 %.2f ms, max %.2f ms, delta max %.3f ms, work mean %.2f ms, p99 "
            "%.2f ms, cpu %.1f%% (%.2f ms/frame), budget slept %.1f ms" % (
                data.get("errors", 0), data.get("slow", 0), data.get("fast", 0),
                data.get("delta_errors", 0), data.get("display_delta_errors", 0),
                data.get("present_errors", 0),
                data.get("frames", 0), data.get("target_fps", 0), display,
                data.get("mean_ms", 0), data.get("p99_ms", 0), data.get("max_ms", 0),
                data.get("delta_max_ms", 0), data.get("work_mean_ms", 0),
                data.get("work_p99_ms", 0), data.get("cpu_percent", 0), data.get("cpu_ms", 0),
                data.get("slept_ms", 0)))


def cull_detail(result):
    return ("visible %d..%d, frustum culled %d..%d, occlusion culled %d..%d, %d of %d samples "
            "changed" % (result["visible_min"], result["visible_max"],
                         result["frustum_culled_min"], result["frustum_culled_max"],
                         result["occlusion_culled_min"], result["occlusion_culled_max"],
                         result["cull_changes"], result["cull_samples"]))


def run_perf_scene(scene, out, args):
    name = scene_name(scene)
    report = os.path.join(out, name + ".json")
    harness = os.path.join(out, name + ".run.flecs")
    result = {"scene": display(scene), "name": name, "status": "fail",
              "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}
    with open(scene) as f:
        if "PerfRun" not in f.read():
            result.update({"detail": "the scene sets no PerfRun", "elapsed": 0})
            return result
    with open(harness, "w") as f:
        f.write(perf_script(scene, report, args))
    result["load_before"] = wait_for_load(args.max_load, args.load_wait)
    start = time.time()
    code, log, run = run_guarded(perf_command(harness, args), args, report, PERF_TIMEOUT * 2,
                                 lambda log: "cpu oversubscribed" in log and
                                 not os.path.exists(report))
    result.update({k: run[k] for k in ("attempts", "refusals", "load_wait", "load_run")})
    result["elapsed"] = round(time.time() - start, 6)
    result["load_after"] = load_average()
    with open(os.path.join(out, name + ".log"), "w") as f:
        f.write(log)
    data = None
    if os.path.isfile(report):
        with open(report) as f:
            data = json.load(f)
    if code != 0 or not data:
        result["detail"] = "run failed (exit %d), see perf/%s.log" % (code, name)
        if run.get("refused"):
            result["detail"] = refusal_detail(run, args)
        print("\n".join("    " + line for line in log.strip().splitlines()[-15:]))
        return result
    for key in PERF_FIELDS:
        if key in data:
            result[key] = data[key]
    result["entities"] = data.get("shapes", 0)
    if data.get("target_fps") and data.get("period"):
        result["deg_per_frame"] = round(360.0 / (data["target_fps"] * data["period"]), 3)
    perf_cull(data, result)
    result["intervals_ms"] = data.get("intervals_ms", [])
    problems = perf_problems(data)
    result["problems"] = problems
    result["status"] = "pass" if data.get("passed") and not problems else "fail"
    result["detail"] = "; ".join(problems + [perf_detail(data), cull_detail(result)])
    return result


def cmd_perf(args):
    directory = os.path.abspath(args.dir) if args.dir else PERF_DIR
    base = os.path.abspath(args.out) if args.out else OUT_DIR
    ignore_out(base)
    out = os.path.join(base, "perf")
    os.makedirs(out, exist_ok=True)
    if args.scenes:
        scenes = [resolve_scene(directory, s) for s in args.scenes]
    else:
        scenes = sorted(glob.glob(os.path.join(directory, "*.flecs")))
    if not scenes:
        sys.exit("no scenes in %s" % directory)
    start = time.time()
    args.binary, _ = prepare_app(args)
    args.load_budget = args.guard_wait
    results = []
    try:
        for scene in scenes:
            r = run_perf_scene(scene, out, args)
            results.append(r)
            print("%s %s (%s, load %s -> %s)" % ("PASS" if r["status"] == "pass" else "FAIL",
                                                  r["scene"], r.get("detail", ""),
                                                  r.get("load_before"), r.get("load_after")),
                  flush=True)
    finally:
        os.unlink(args.binary)
    failed = sum(1 for r in results if r["status"] != "pass")
    path = os.path.join(base, "perf.json")
    with open(path, "w") as f:
        json.dump({"kind": "perf", "timestamp": results[0]["timestamp"],
                   "cores": os.cpu_count(), "results": results}, f, indent=2)
        f.write("\n")
    print("%d passed, %d failed, report %s" % (len(results) - failed, failed, display(path)))
    if args.json:
        cases = [dict(r, scene=r["name"]) for r in results]
        write_json(args.json, cases, display(directory), time.time() - start, "Perf",
                   PERF_KEYS)

    return 1 if failed else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="kind", required=True)
    visual = sub.add_parser("visual", help="render the visual scenes and compare the baselines")
    visual.add_argument("scenes", nargs="*", help="scene names or paths, all scenes when empty")
    visual.add_argument("--dir", help="folder of the scenes, test/integration/visual by default")
    visual.add_argument("--app", default=DEFAULT_APP, help="bake project that renders the scenes")
    visual.add_argument("--env", help="bake3 local environment, the default one when absent")
    visual.add_argument("--cfg", help="bake3 build mode, the bake3 default when absent")
    visual.add_argument("--out", help="folder of the renders and the report")
    visual.add_argument("--frames", type=int, default=3, help="frames to run before the capture")
    visual.add_argument("--size", default="640x360", help="surface size in pixels")
    visual.add_argument("--update-baseline", action="append", metavar="SCENE",
                        help="store the render of this scene as its baseline")
    visual.add_argument("--json", help="write the results in the bake3 test report format")
    visual.add_argument("--load-wait", dest="guard_wait", type=float, default=LOAD_WAIT,
                        help="seconds the whole run waits at most for the load average to drop "
                        "to the core count when the load guard refuses a render")
    visual.add_argument("--history", help="folder that keeps every run, failures with their "
                        "renders, test/integration/out/history by default")
    visual.set_defaults(fn=cmd_visual)
    perf = sub.add_parser("perf", help="run the perf scenes and check every frame time")
    perf.add_argument("scenes", nargs="*", help="scene names or paths, all scenes when empty")
    perf.add_argument("--dir", help="folder of the scenes, test/integration/perf by default")
    perf.add_argument("--app", default=DEFAULT_APP, help="bake project that runs the scenes")
    perf.add_argument("--env", help="bake3 local environment, the default one when absent")
    perf.add_argument("--cfg", default="release",
                      help="bake3 build mode the scenes run in, release by default")
    perf.add_argument("--out", help="folder of perf.json, the runs go to its perf/ folder")
    perf.add_argument("--size", default="1280x720",
                      help="surface size in pixels of the scenes that declare no Surface")
    perf.add_argument("--seconds", type=float,
                      help="measure this many seconds at the target of every scene (frames)")
    perf.add_argument("--cpu-budget", type=float, help="App.cpu_budget of the runs in percent")
    perf.add_argument("--stall", type=float, default=0,
                      help="busy milliseconds added to every frame, to check that slow fails")
    perf.add_argument("--max-load", type=float,
                      help="wait until the 1 minute load average is at most this")
    perf.add_argument("--load-wait", type=float, default=300,
                      help="seconds --max-load waits at most before running anyway")
    perf.add_argument("--guard-wait", type=float, default=LOAD_WAIT,
                      help="seconds the whole run waits at most for the load average to drop "
                      "to the core count when the load guard refuses a run")
    perf.add_argument("--json", help="write the results in the bake3 test report format")
    perf.set_defaults(fn=cmd_perf)
    args = parser.parse_args()

    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
