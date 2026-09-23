import argparse
import json
import math
import re
import statistics
import time
import urllib.parse
import urllib.request
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description="Measure wall-clock frame throughput of a running Box window.")
    parser.add_argument("--port", type=int, default=27751)
    parser.add_argument("--size", default="2560x1440")
    parser.add_argument("--warmup", type=float, default=10)
    parser.add_argument("--seconds", type=float, default=20)
    parser.add_argument("--orbit", action="store_true")
    parser.add_argument("--orbit-radius", type=float, default=65)
    parser.add_argument("--orbit-height", type=float, default=20)
    parser.add_argument("--orbit-target-height", type=float, default=80)
    parser.add_argument("--orbit-period", type=float, default=30)
    parser.add_argument("--orbit-hz", type=float, default=20)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    width, height = map(int, args.size.split("x"))
    if min(width, height, args.seconds) <= 0 or args.warmup < 0:
        parser.error("size and duration must be positive; warmup cannot be negative")
    if args.orbit and min(args.orbit_radius, args.orbit_period, args.orbit_hz) <= 0:
        parser.error("orbit radius, period and update rate must be positive")
    base = f"http://localhost:{args.port}/"

    def request(path, params=None, body=None):
        url = base + path
        if params:
            url += "?" + urllib.parse.urlencode(params)
        data = body.encode() if body is not None else None
        with urllib.request.urlopen(url, data=data, timeout=30) as response:
            return response.read().decode()

    session = request("terminal/new").strip()

    def command(text):
        return request("terminal/exec", {"session": session}, text)

    def frame():
        begin = time.perf_counter()
        app = command("get box.core.App box.core.App")
        end = time.perf_counter()
        if "headless: true" in app:
            raise RuntimeError("This measurement requires a windowed application")
        count = int(re.search(r"\bframe: (\d+)", app)[1])
        return (begin + end) * 0.5, count

    def orbit(elapsed):
        angle = elapsed * 2 * math.pi / args.orbit_period
        x = args.orbit_radius * math.cos(angle)
        z = args.orbit_radius * math.sin(angle)
        command(f"set camera box.transform.Position {{x:{x:.6f},y:{args.orbit_height:.6f},z:{z:.6f}}}")

    try:
        frame()
        command(f"set box.surface.Surface box.surface.Surface {{width:{width},height:{height},resolution_scale:1,vsync:false}}")
        if args.orbit:
            command(f"set camera box.transform.LookAt {{x:0,y:{args.orbit_target_height:.6f},z:0}}")
            orbit(0)
        time.sleep(args.warmup)
        surface = command("get box.surface.Surface box.surface.Surface")
        camera = {
            "position": command("get camera box.transform.Position"),
            "look_at": command("get camera box.transform.LookAt"),
        }
        samples = []
        first_time, first_frame = frame()
        previous_time, previous_frame = first_time, first_frame
        if args.orbit:
            next_orbit = first_time
            next_sample = first_time + 0.5
            while previous_time - first_time < args.seconds:
                now = time.perf_counter()
                if now >= next_orbit:
                    orbit(now - first_time)
                    next_orbit += 1 / args.orbit_hz
                    if next_orbit < now:
                        next_orbit = now + 1 / args.orbit_hz
                if now >= next_sample:
                    current_time, current_frame = frame()
                    seconds = current_time - previous_time
                    samples.append({"seconds": seconds, "frames": current_frame - previous_frame,
                                    "fps": (current_frame - previous_frame) / seconds,
                                    "angle_deg": ((current_time - first_time) * 360 / args.orbit_period) % 360})
                    previous_time, previous_frame = current_time, current_frame
                    next_sample = current_time + 0.5
                else:
                    time.sleep(min(next_orbit, next_sample) - now)
        else:
            while previous_time - first_time < args.seconds:
                time.sleep(0.5)
                current_time, current_frame = frame()
                seconds = current_time - previous_time
                samples.append({"seconds": seconds, "frames": current_frame - previous_frame,
                                "fps": (current_frame - previous_frame) / seconds})
                previous_time, previous_frame = current_time, current_frame
        fps = (previous_frame - first_frame) / (previous_time - first_time)
        report = {"requested_points": [width, height], "surface": surface.strip(),
                  "camera": camera, "seconds": previous_time - first_time,
                  "frames": previous_frame - first_frame, "fps": fps,
                  "mean_ms": 1000 / fps,
                  "median_interval_fps": statistics.median(s["fps"] for s in samples),
                  "samples": samples}
        if args.orbit:
            report["orbit"] = {"radius": args.orbit_radius, "height": args.orbit_height,
                               "target_height": args.orbit_target_height,
                               "period": args.orbit_period, "update_hz": args.orbit_hz}
        output = Path(args.out)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2) + "\n")
        print(f"{fps:.2f} FPS, {1000 / fps:.3f} ms; {previous_frame - first_frame} frames")
        print(surface.strip())
    finally:
        request("terminal/close", {"session": session})


if __name__ == "__main__":
    main()
