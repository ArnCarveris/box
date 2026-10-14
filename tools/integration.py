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
"""
import argparse
import glob
import html
import json
import os
import shutil
import struct
import subprocess
import sys
import time
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VISUAL_DIR = os.path.join(ROOT, "test", "integration", "visual")
OUT_DIR = os.path.join(ROOT, "test", "integration", "out")
DEFAULT_APP = "apps/boxrun"
MAX_DELTA = 24
PIXEL_DELTA = 16
MAX_PIXELS = 200
RENDER_TIMEOUT = 120
GUARD_RETRIES = 6
GUARD_WAIT = 10
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


def render_scene(scene, png, args):
    width, height = (int(v) for v in args.size.lower().split("x"))
    with open(scene) as f:
        source = f.read()
    harness = os.path.join(os.path.dirname(png), scene_name(scene) + ".run.flecs")
    with open(harness, "w") as f:
        f.write("%s\n\nvisual_capture {\n  box.surface.Surface: {width: %d, height: %d, "
                "write_to_file: \"%s\"}\n}\n" % (source.rstrip(), width, height, png))
    env = "--local-env=" + args.env if args.env else "--local-env"
    cmd = ["bake3", "run", app_project(args.app), env]
    if args.cfg:
        cmd += ["--cfg", args.cfg]
    cmd += ["--", "--headless", "--frames", str(args.frames), "--timeout", str(RENDER_TIMEOUT),
            "--port", "0", harness]
    log = ""
    for attempt in range(GUARD_RETRIES):
        if os.path.exists(png):
            os.unlink(png)
        try:
            proc = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True,
                                  timeout=RENDER_TIMEOUT * 2)
            log = proc.stdout + proc.stderr
            code = proc.returncode
        except subprocess.TimeoutExpired as e:
            log = "render timed out\n" + str(e.stdout or "") + str(e.stderr or "")
            code = -1
        if "cpu oversubscribed" not in log or attempt + 1 == GUARD_RETRIES:
            break
        time.sleep(GUARD_WAIT)
    with open(os.path.join(os.path.dirname(png), scene_name(scene) + ".log"), "w") as f:
        f.write(log)
    return code == 0 and os.path.isfile(png), log


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
    rendered, log = render_scene(scene, png, args)
    result = {"scene": name, "path": display(scene), "status": "fail",
              "image": rendered, "baseline": False, "diff": False}
    if not rendered:
        result["detail"] = "render failed, see %s.log" % name
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


def write_json(path, results, project, elapsed):
    tests = []
    for r in results:
        row = {"suite": "Visual", "case": r["scene"], "status": r["status"],
               "elapsed": r["elapsed"], "detail": r.get("detail", "")}
        for key in ("max", "mean", "px"):
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
    results = []
    for scene in scenes:
        r = run_scene(scene, baseline_dir, out, scene_name(scene) in updates, args)
        results.append(r)
        print("%s %s (%s)" % ("PASS" if r["status"] == "pass" else "FAIL", r["path"],
                              r.get("detail", "")))
    report = write_report(results, out, baseline_dir)
    failed = sum(1 for r in results if r["status"] != "pass")
    print("%d passed, %d failed, report %s" % (len(results) - failed, failed,
                                               display(report)))
    if args.json:
        write_json(args.json, results, display(directory), time.time() - start)

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
    visual.set_defaults(fn=cmd_visual)
    args = parser.parse_args()

    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
