#!/usr/bin/env python3
"""Runs integration test scenes: visual scenes to screenshots, bench scenes to JSON
reports and command scenes to session command logs.

Usage:
  integration.py visual  [--out dir] [--env name] [--frames n] [--update-baseline]
                         [--against dir] [scene.flecs ...]
  Visual scenes are compared with test/integration/visual/baseline/<scene>.png and fail only when the
  largest channel delta exceeds 24 and more than 200 pixels differ by more than 16 (GPU speckle passes);
  a missing baseline is created, --update-baseline replaces the baselines of the scenes given.
  --against compares with the <scene>.png renders of an earlier run in that directory instead, which
  checks a change against what the code before it drew and leaves the baselines alone. A run writes
  <out>/index.html, a page with the baseline, the render and a diff image of every scene, failures
  first; the diff is green where the render is brighter, red where it is darker, faint where the
  delta stays under 16 and a dimmed grey copy of the baseline where the pixels are identical.
  integration.py bench   [--out dir] [--env name] [--frames n] [--spike factor]
                         [--no-attribute] [--fps n] [scene.flecs ...]
  Bench scenes run under a fixed step and fail when a frame exceeded the spike budget, which is
  a multiple of the mean frame time of the run; the report names the offending frames and the
  system each of them spent the most time in. A <scene>.args next to a scene holds the extra
  box_bench arguments of that scene, one per line.
  integration.py command [--out dir] [--env name] [--frames n] [scene.flecs ...]

A command scene holds a terminal named terminals.agent0 with a CommandScript that
points at the <scene>.cmd next to it. The scene runs headless with a generated
harness scene that sets Terminal.log, so the session log survives the process
folder; the log is written to <out>/<scene>.log, with the absolute path of the
repository stripped from it, and compared with the <scene>.expected next to the
scene when that file exists. A <scene>_content.flecs
next to a scene is loaded before it, which keeps scripts that commands patch out
of the scene that holds the terminal. The PNGs that render commands write to the
session resources are copied to <out>/<scene>_<image>.png while the scene runs,
because the process removes its folder when it exits.
"""
import argparse
import difflib
import glob
import json
import os
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VISUAL_DIR = os.path.join(ROOT, "test", "integration", "visual")
BENCH_DIR = os.path.join(ROOT, "test", "integration", "bench")
COMMAND_DIR = os.path.join(ROOT, "test", "integration", "command")
HARNESS = "using box.command\nusing box.script\n$ { ScriptSettings: {enabled: false} }\n\nterminals {\n  agent0 {\n    Terminal: {log: \"%s\"}\n  }\n}\n"
DOTTED_ROOT = ROOT.replace(".", "\\.").replace("/", ".") + "."
PNG_END = b"IEND\xaeB`\x82"


def bake(args, cwd=None):
    return subprocess.run(["bake3", *args], cwd=cwd or ROOT, text=True, capture_output=True)


def env_flag(name):
    return f"--local-env={name}" if name else "--local-env"


BASELINE_DIR = os.path.join(VISUAL_DIR, "baseline")
MAX_DELTA = 24
PIXEL_DELTA = 16
MAX_PIXELS = 200


def read_png(path):
    """Decode an 8 bit RGB or RGBA PNG into an (h, w, 3) uint8 array without external libraries."""
    import struct
    import zlib
    import numpy as np
    with open(path, "rb") as f:
        data = f.read()
    pos = 8
    idat = b""
    width = height = channels = 0
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, color = struct.unpack(">IIBB", body[:10])
            channels = {2: 3, 6: 4, 0: 1, 4: 2}[color]
            if depth != 8:
                raise ValueError("%s: unsupported bit depth %d" % (path, depth))
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * channels
    rows = np.zeros((height, stride), dtype=np.uint8)
    prev = np.zeros(stride, dtype=np.int32)
    offset = 0
    for y in range(height):
        kind = raw[offset]
        line = np.frombuffer(raw, dtype=np.uint8, count=stride, offset=offset + 1).astype(np.int32)
        offset += stride + 1
        if kind == 1:
            for x in range(channels, stride):
                line[x] = (line[x] + line[x - channels]) & 255
        elif kind == 2:
            line = (line + prev) & 255
        elif kind == 3:
            for x in range(stride):
                left = line[x - channels] if x >= channels else 0
                line[x] = (line[x] + ((left + prev[x]) >> 1)) & 255
        elif kind == 4:
            for x in range(stride):
                a = line[x - channels] if x >= channels else 0
                b = prev[x]
                c = prev[x - channels] if x >= channels else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[x] = (line[x] + pred) & 255
        rows[y] = line
        prev = line
    pixels = rows.reshape(height, width, channels)
    return pixels[:, :, :3] if channels >= 3 else np.repeat(pixels[:, :, :1], 3, axis=2)


def compare_images(baseline_path, actual_path):
    """Compare two PNGs the way flecs-engine did: fail only when the difference is strong and widespread."""
    import numpy as np
    base = read_png(baseline_path)
    actual = read_png(actual_path)
    if base.shape != actual.shape:
        return {"ok": False, "reason": "size %s != %s" % (base.shape[:2], actual.shape[:2])}
    diff = np.abs(base.astype(np.int16) - actual.astype(np.int16))
    max_delta = int(diff.max())
    mean_delta = float(diff.mean())
    npx = int((diff.max(axis=2) > PIXEL_DELTA).sum())
    ok = not (max_delta > MAX_DELTA and npx > MAX_PIXELS)
    return {"ok": ok, "max": max_delta, "mean": round(mean_delta, 4), "px": npx}


def compare_baseline(name, png, update, against=None):
    """Store or compare a render against test/integration/visual/baseline/<name>.png, or against
    the render of the same scene in the directory of an earlier run."""
    if against:
        baseline = os.path.join(against, name + ".png")
        if not os.path.isfile(baseline):
            return {"ok": False, "status": "no render in %s" % against, "path": None}
        result = compare_images(baseline, png)
        result["status"] = "match" if result["ok"] else "differs"
        result["path"] = baseline
        return result
    baseline = os.path.join(BASELINE_DIR, name + ".png")
    os.makedirs(BASELINE_DIR, exist_ok=True)
    if update or not os.path.isfile(baseline):
        status = "updated" if os.path.isfile(baseline) else "new"
        with open(png, "rb") as src, open(baseline, "wb") as dst:
            dst.write(src.read())
        return {"ok": True, "status": status, "path": baseline}
    result = compare_images(baseline, png)
    result["status"] = "match" if result["ok"] else "differs"
    result["path"] = baseline
    return result


def write_png(path, pixels):
    """Encode an (h, w, 3) uint8 array as an RGB PNG without external libraries."""
    import struct
    import zlib
    import numpy as np
    height, width = pixels.shape[:2]
    rows = np.zeros((height, width * 3 + 1), dtype=np.uint8)
    rows[:, 1:] = pixels.reshape(height, width * 3)

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(rows.tobytes(), 6)))
        f.write(chunk(b"IEND", b""))


def write_diff(baseline_path, actual_path, out_path):
    """Write the diff image of a render: green where it is brighter than the baseline, red where it
    is darker, yellow where only the hue moved, at half strength when no channel moved more than
    PIXEL_DELTA, over a dimmed grey copy of the baseline where the pixels are identical."""
    import numpy as np
    base = read_png(baseline_path).astype(np.int32)
    actual = read_png(actual_path).astype(np.int32)
    if base.shape != actual.shape:
        return False
    luma_base = base[:, :, 0] * 30 + base[:, :, 1] * 59 + base[:, :, 2] * 11
    luma_actual = actual[:, :, 0] * 30 + actual[:, :, 1] * 59 + actual[:, :, 2] * 11
    delta = np.abs(base - actual).max(axis=2)
    grey = (luma_base // 100 // 4).astype(np.uint8)
    out = np.repeat(grey[:, :, None], 3, axis=2)
    strength = np.where(delta > PIXEL_DELTA, 255, 128).astype(np.uint8)
    brighter = (delta > 0) & (luma_actual > luma_base)
    darker = (delta > 0) & (luma_actual < luma_base)
    shifted = (delta > 0) & (luma_actual == luma_base)
    out[brighter] = 0
    out[brighter, 1] = strength[brighter]
    out[darker] = 0
    out[darker, 0] = strength[darker]
    out[shifted, 0] = strength[shifted]
    out[shifted, 1] = strength[shifted]
    out[shifted, 2] = 0
    write_png(out_path, out)
    return True


def write_report(results, out, against):
    """Write <out>/index.html with the baseline, the render and the diff of every scene."""
    import html
    rows = []
    for r in sorted(results, key=lambda r: r["status"] == "pass"):
        name = os.path.splitext(os.path.basename(r["scene"]))[0]
        cmp = r["baseline"]
        current = os.path.join(out, name + ".png") if r["image"] else None
        baseline = None
        diff = None
        if cmp.get("path") and os.path.isfile(cmp["path"]):
            baseline = os.path.join(out, name + "_baseline.png")
            if os.path.abspath(cmp["path"]) != os.path.abspath(baseline):
                with open(cmp["path"], "rb") as src, open(baseline, "wb") as dst:
                    dst.write(src.read())
            if current and write_diff(baseline, current, os.path.join(out, name + "_diff.png")):
                diff = os.path.join(out, name + "_diff.png")
        detail = cmp.get("status", "")
        if "max" in cmp:
            detail = "max %d, mean %.4f, %d px &gt; %d" % (cmp["max"], cmp["mean"], cmp["px"], PIXEL_DELTA)
        elif "reason" in cmp:
            detail = html.escape(cmp["reason"])
        cells = []
        for path in (baseline, current, diff):
            if path:
                ref = html.escape(os.path.basename(path))
                cells.append('<td><a href="%s" target="_blank"><img src="%s" loading="lazy"></a></td>'
                             % (ref, ref))
            else:
                cells.append('<td><span class="placeholder">&mdash;</span></td>')
        rows.append('<tr class="%s"><td class="name">%s<br><span class="%s">%s</span><br>'
                    '<span class="detail">%s</span></td>%s</tr>' % (
                        r["status"], html.escape(name), r["status"],
                        "pass" if r["status"] == "pass" else "FAIL", detail, "".join(cells)))
    failed = sum(1 for r in results if r["status"] != "pass")
    source = html.escape(against) if against else "test/integration/visual/baseline"
    page = [
        '<!DOCTYPE html>',
        '<html><head><meta charset="utf-8"><title>Box visual scenes</title>',
        '<style>',
        'body { font-family: -apple-system, sans-serif; background: #1a1a1a; color: #eee; margin: 20px; }',
        'h1 { font-weight: 400; } p { color: #aaa; }',
        'table { border-collapse: collapse; width: 100%; }',
        'th, td { padding: 10px; border-bottom: 1px solid #333; vertical-align: top; }',
        'th { text-align: left; background: #222; font-weight: 600; position: sticky; top: 0; }',
        'img { max-width: 480px; width: 100%; display: block; background: #000; }',
        '.name { font-weight: 600; min-width: 180px; }',
        '.pass { color: #8f8; } .fail { color: #f88; }',
        '.detail { color: #ccc; font-size: 0.9em; font-weight: 400; }',
        '.placeholder { color: #555; font-style: italic; }',
        'label { color: #ccc; }',
        'body.failures tr.pass { display: none; }',
        '</style></head><body>',
        '<h1>Visual scenes &mdash; %d failed of %d</h1>' % (failed, len(results)),
        '<p>Baseline: %s. Diff: green brighter, red darker, faint under a delta of %d, '
        'grey identical.</p>' % (source, PIXEL_DELTA),
        '<p><label><input type="checkbox" onchange="document.body.classList.toggle(\'failures\', '
        'this.checked)"> Only failures</label></p>',
        '<table>',
        '<tr><th>Scene</th><th>Baseline</th><th>Current</th><th>Diff</th></tr>',
        *rows,
        '</table></body></html>',
    ]
    path = os.path.join(out, "index.html")
    with open(path, "w") as f:
        f.write("\n".join(page))
    print("report %s" % path)


def visual_scripts(scene):
    sources_path = os.path.splitext(scene)[0] + ".sources"
    sources = []
    if os.path.isfile(sources_path):
        base = os.path.dirname(sources_path)
        with open(sources_path) as f:
            for path in f.read().splitlines():
                path = path.strip()
                if path:
                    sources.append(os.path.abspath(os.path.join(base, path)))
    return [*sources, os.path.abspath(scene)]


def run_visual(scenes, out, env, frames, width, height, update=False, against=None):
    os.makedirs(out, exist_ok=True)
    results = []
    for scene in scenes:
        name = os.path.splitext(os.path.basename(scene))[0]
        png = os.path.abspath(os.path.join(out, name + ".png"))
        with tempfile.NamedTemporaryFile("w", suffix=".flecs", delete=False) as harness:
            harness.write("using box.surface\nusing box.script\nusing box.job\n$ { ScriptSettings: {enabled: false} }\n$ { JobSettings: {workers: -1, budget_ms: 0} }\n$ { Surface: {width: %d, height: %d, write_to_file: \"%s\"} }\n"
                          % (width, height, png))
            harness_path = harness.name
        proc = bake(["run", "box_app", env_flag(env), "--cfg", "release", "--",
                     "--headless", "--frames", str(frames), harness_path, *visual_scripts(scene)])
        os.unlink(harness_path)
        rendered = proc.returncode == 0 and os.path.isfile(png)
        cmp = (compare_baseline(name, png, update, against) if rendered
               else {"ok": False, "status": "missing"})
        ok = rendered and cmp["ok"]
        detail = cmp["status"]
        if "max" in cmp:
            detail = "max %d mean %.4f px>%d %d %s" % (cmp["max"], cmp["mean"], PIXEL_DELTA, cmp["px"], cmp["status"])
        results.append({"scene": os.path.relpath(scene, ROOT), "status": "pass" if ok else "fail",
                        "baseline": cmp, "image": os.path.relpath(png, ROOT) if rendered else None})
        print(f"{'PASS' if ok else 'FAIL'} {os.path.relpath(scene, ROOT)} ({detail})")
        if not rendered:
            print(proc.stdout[-2000:], proc.stderr[-2000:])
    write_report(results, out, against)
    return results


def bench_args(scene):
    """Read the extra box_bench arguments of a scene from the <scene>.args next to it."""
    path = os.path.splitext(scene)[0] + ".args"
    if not os.path.isfile(path):
        return []
    return open(path).read().split()


def bench_spike_line(spike):
    """Format one frame that exceeded the spike budget."""
    return "  SPIKE frame %s at %.2f ms, top system %s (%.2f ms)" % (
        spike.get("frame"), spike.get("ms", 0), spike.get("system") or "unattributed",
        spike.get("system_ms", 0))


def bench_summary(data):
    """Format the frame distribution and the spike verdict of a bench report."""
    spikes = data.get("spikes") or []
    worst = ", worst in %s" % (spikes[0].get("system") or "unattributed") if spikes else ""
    return ("mean %.2f ms, p99 %.2f ms, max %.2f ms, %.0f fps, budget %.2f ms, %d spike(s)%s" % (
        data.get("mean_ms", 0), data.get("p99_ms", 0), data.get("max_ms", 0), data.get("fps", 0),
        data.get("spike_budget_ms", 0), data.get("spike_count", 0), worst))


def run_bench(scenes, out, env, frames, extra):
    os.makedirs(out, exist_ok=True)
    results = []
    for scene in scenes:
        name = os.path.splitext(os.path.basename(scene))[0]
        report = os.path.abspath(os.path.join(out, name + ".json"))
        if os.path.isfile(report):
            os.unlink(report)
        proc = bake(["run", "box_bench", env_flag(env), "--cfg", "release", "--",
                     os.path.abspath(scene), "--frames", str(frames), "--report", report,
                     *extra, *bench_args(scene)])
        data = json.load(open(report)) if os.path.isfile(report) else {}
        spikes = int(data.get("spike_count") or 0)
        ok = bool(data) and proc.returncode == 0 and not spikes
        row = dict(data)
        row["scene"] = os.path.relpath(scene, ROOT)
        row["status"] = "pass" if ok else "fail"
        results.append(row)
        print(f"{'PASS' if ok else 'FAIL'} {os.path.relpath(scene, ROOT)} "
              f"{bench_summary(data) if data else ''}")
        for spike in data.get("spikes") or []:
            print(bench_spike_line(spike))
        if not data:
            print(proc.stdout[-2000:], proc.stderr[-2000:])
    return results


def command_scripts(scene):
    content = os.path.splitext(scene)[0] + "_content.flecs"
    scripts = [content] if os.path.isfile(content) else []
    return [os.path.abspath(f) for f in scripts + [scene]]


def command_normalize(log):
    text = open(log).read()
    stripped = text.replace(DOTTED_ROOT, "").replace(ROOT + "/", "")
    if stripped != text:
        open(log, "w").write(stripped)


def command_diff(log, expected):
    actual = [line.rstrip() for line in open(log).read().splitlines()]
    want = [line.rstrip() for line in open(expected).read().splitlines()]
    return list(difflib.unified_diff(want, actual, os.path.basename(expected),
                                     os.path.basename(log), lineterm=""))


def command_images(out, name, start, seen):
    """Copy the images a scene rendered, which its process removes when it exits.

    A file is copied once it ends with the PNG end marker, because the renderer
    writes it while the scene keeps running.
    """
    for png in sorted(glob.glob("/tmp/box/*/*/resources/*.png")):
        if png in seen or os.path.getmtime(png) < start:
            continue
        with open(png, "rb") as f:
            data = f.read()
        if not data.endswith(PNG_END):
            continue
        seen.add(png)
        with open(os.path.join(out, f"{name}_{os.path.basename(png)}"), "wb") as f:
            f.write(data)


def run_command(scenes, out, env, frames):
    os.makedirs(out, exist_ok=True)
    results = []
    for scene in scenes:
        name = os.path.splitext(os.path.basename(scene))[0]
        log = os.path.abspath(os.path.join(out, name + ".log"))
        if os.path.exists(log):
            os.unlink(log)
        with tempfile.NamedTemporaryFile("w", suffix=".flecs", delete=False) as harness:
            harness.write(HARNESS % log)
            harness_path = harness.name
        seen, start = set(), time.time()
        with tempfile.TemporaryFile("w+") as output:
            proc = subprocess.Popen(["bake3", "run", "box_app", env_flag(env), "--cfg", "release", "--",
                                     "--headless", "--frames", str(frames), "--fps", "60", "--port", "0",
                                     *command_scripts(scene), harness_path],
                                    cwd=ROOT, text=True, stdout=output, stderr=subprocess.STDOUT)
            while proc.poll() is None:
                command_images(out, name, start, seen)
                time.sleep(0.01)
            command_images(out, name, start, seen)
            output.seek(0)
            printed = output.read()
        os.unlink(harness_path)
        ok = proc.returncode == 0 and os.path.isfile(log)
        if os.path.isfile(log):
            command_normalize(log)
        expected = os.path.join(os.path.dirname(scene), name + ".expected")
        diff = command_diff(log, expected) if ok and os.path.isfile(expected) else []
        ok = ok and not diff
        results.append({"scene": os.path.relpath(scene, ROOT), "status": "pass" if ok else "fail",
                        "log": os.path.relpath(log, ROOT) if os.path.isfile(log) else None,
                        "expected": os.path.relpath(expected, ROOT) if os.path.isfile(expected) else None,
                        "images": [f"{name}_{os.path.basename(p)}" for p in sorted(seen)]})
        print(f"{'PASS' if ok else 'FAIL'} {os.path.relpath(scene, ROOT)}")
        if diff:
            print("\n".join(diff))
        elif not ok:
            print(printed[-2000:])
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("kind", choices=["visual", "bench", "command"])
    parser.add_argument("scenes", nargs="*")
    parser.add_argument("--out")
    parser.add_argument("--env")
    parser.add_argument("--frames", type=int)
    parser.add_argument("--size", default="960x540")
    parser.add_argument("--churn", default="1-1000")
    parser.add_argument("--churn-color", action="store_true")
    parser.add_argument("--spike", default="4", help="spike budget as a multiple of the mean")
    parser.add_argument("--no-attribute", action="store_true",
                        help="do not attribute a frame to its slowest system")
    parser.add_argument("--fps", default="60", help="fixed step of a bench run, 0 measures it")
    parser.add_argument("--update-baseline", action="store_true")
    parser.add_argument("--against", help="compare visual scenes with the renders of an earlier run")
    args = parser.parse_args()

    if args.kind == "visual":
        scenes = args.scenes or sorted(glob.glob(os.path.join(VISUAL_DIR, "*.flecs")))
        width, height = (int(v) for v in args.size.split("x"))
        results = run_visual(scenes, args.out or os.path.join(ROOT, "test", "integration", "out"), args.env,
                             args.frames or 3, width, height, update=args.update_baseline,
                             against=os.path.abspath(args.against) if args.against else None)
    elif args.kind == "command":
        scenes = args.scenes or sorted(s for s in glob.glob(os.path.join(COMMAND_DIR, "*.flecs"))
                                       if not s.endswith("_content.flecs"))
        results = run_command(scenes, args.out or os.path.join(ROOT, "test", "integration", "out"), args.env,
                              args.frames or 24)
    else:
        scenes = args.scenes or sorted(glob.glob(os.path.join(BENCH_DIR, "*.flecs")))
        extra = ["--churn", args.churn, "--spike", args.spike, "--fps", args.fps]
        extra += ["--churn-color"] if args.churn_color else []
        extra += [] if args.no_attribute else ["--attribute"]
        results = run_bench(scenes, args.out or os.path.join(ROOT, "test", "integration", "out"), args.env,
                            args.frames or 300, extra)

    with open(os.path.join(args.out or os.path.join(ROOT, "test", "integration", "out"), f"{args.kind}.json"), "w") as f:
        json.dump({"kind": args.kind, "results": results}, f, indent=2)
    return 0 if all(r["status"] == "pass" for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
