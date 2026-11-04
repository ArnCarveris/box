"""The audio command over the shell endpoints: the status and the buses, the master volume and
mute, and a capture of a known tone to a WAV resource.

Every case runs in a fresh headless apps/boxrun process with test/assets/audio/tone.flecs (named
from the repository root): a looping 440 Hz tone at a peak of 0.25 on the bus music. Headless
runs open no device and render with the null transport at 48000 Hz, which is not an error: the
runner fails a case on any logged error.

test_all_features starts two more processes of its own with test/assets/audio/all_features.flecs,
the scene that uses every part of box.audio, which captures its first 3 seconds at load: both
channels have their levels, the reverb tail rings after a click, the engine is on the left and
the chime of play_at on the right, and the two runs write bit identical captures.
"""
import io
import math
import os
import re
import struct
import wave

import box_client
from box_client import parse_value

APP = "apps/boxrun"
ARGS = ["test/assets/audio/tone.flecs"]

TONE = 440.0
PEAK = 0.25
RATE = 48000
SECONDS = 0.5


def capture(app, seconds=SECONDS, frames=600):
    """Run audio capture and wait for the WAV resource: (channels, rate, left samples)."""
    line = "audio capture %g" % seconds
    name = parse_value(app.exec(line))
    app.check(isinstance(name, str) and name, "audio capture returned %r" % name)
    start = None
    while True:
        status, body = app.request("shell/session/%s/resource/%s" % (app.session, name),
                                   binary=True)
        if status == 200 and body:
            break
        frame = app.frame()
        start = frame if start is None else start
        if frame - start > frames:
            app.fail("%s did not arrive within %d frames" % (name, frames), line, name)
    if app.out:
        with open("%s/%s.%s.wav" % (app.out, app.prefix, name.replace("/", "_")), "wb") as f:
            f.write(body)
    with wave.open(io.BytesIO(body)) as w:
        app.check(w.getsampwidth() == 2, "the capture has %d bytes per sample" %
                  w.getsampwidth())
        channels, rate, count = w.getnchannels(), w.getframerate(), w.getnframes()
        data = w.readframes(count)
    samples = struct.unpack("<%dh" % (count * channels), data)
    return channels, rate, [s / 32767.0 for s in samples[::channels]]


def frequency(x):
    crossings = [i - 1 + x[i - 1] / (x[i - 1] - x[i]) for i in range(1, len(x))
                 if x[i - 1] < 0 <= x[i]]
    if len(crossings) < 2:
        return 0.0
    return (len(crossings) - 1) * RATE / (crossings[-1] - crossings[0])


def test_status(app):
    out = parse_value(app.exec("audio"))
    app.check(out.startswith("no device, 48000 Hz, 1 voices"),
              "audio printed %r, expected the null output with one voice" % out)
    clock = re.search(r"clock: (\d+) frames", out)
    app.check(clock and int(clock.group(1)) > 0, "audio does not show the clock: %r" % out)
    app.check("master: volume 1.00" in out, "audio does not show the master: %r" % out)
    app.check("music: group volume 1.00 pan 0.00 width 1.00 -> master" in out,
              "audio does not show the bus music: %r" % out)
    state = app.exec("get $ box.audio.AudioStatus.state")
    app.check(state == '"AudioNoDevice"', "AudioStatus.state is %s" % state)


def test_volume_mute(app):
    out = parse_value(app.exec("audio volume 0.5"))
    app.check(out == "master volume 0.50", "audio volume printed %r" % out)
    volume = float(app.exec("get $ box.audio.AudioSettings.master_volume"))
    app.check(abs(volume - 0.5) < 1e-6, "AudioSettings.master_volume is %g" % volume)
    out = parse_value(app.exec("audio mute on"))
    app.check(out == "mute on", "audio mute on printed %r" % out)
    app.check(app.exec("get $ box.audio.AudioSettings.mute") == "true", "mute is not set")
    out = parse_value(app.exec("audio mute"))
    app.check(out == "mute off", "audio mute printed %r" % out)


def test_capture_tone(app):
    app.wait_frames(2)
    channels, rate, left = capture(app)
    app.check(channels == 2 and rate == RATE, "the capture is %d channels at %d Hz" %
              (channels, rate))
    app.check(len(left) == int(SECONDS * RATE), "the capture has %d frames" % len(left))
    peak = max(abs(v) for v in left)
    app.check(abs(peak - PEAK) < 0.01, "the tone peaks at %.4f, expected %.2f" % (peak, PEAK))
    f = frequency(left)
    app.check(abs(f - TONE) < 2, "the tone is %.1f Hz, expected %.0f" % (f, TONE))


def test_capture_volume(app):
    app.exec("audio volume 0.5")
    app.wait_frames(2)
    channels, rate, left = capture(app)
    peak = max(abs(v) for v in left)
    app.check(abs(peak - PEAK / 2) < 0.01, "at volume 0.5 the tone peaks at %.4f" % peak)
    app.exec("audio mute on")
    app.wait_frames(2)
    channels, rate, left = capture(app, 0.2)
    peak = max(abs(v) for v in left)
    app.check(peak == 0, "muted the capture peaks at %.4f" % peak)


def test_no_device_no_error(app):
    out = app.exec("logs warning")
    app.check("box.audio" not in out, "the app logged %s without a device" % out)
    app.check(math.isfinite(float(app.exec("get $ box.audio.AudioStatus.load"))),
              "AudioStatus.load is not a number")


ALL_FEATURES = ["test/assets/audio/all_features.flecs"]
ALL_FEATURES_WAV = "/tmp/box_audio_all_features.wav"
ALL_FEATURES_SECONDS = 3.0


def all_features_run(app, run, frames=900):
    """Run all_features.flecs in a process of its own until it wrote its capture: the bytes of
    the WAV and the output of the audio command."""
    if os.path.exists(ALL_FEATURES_WAV):
        os.unlink(ALL_FEATURES_WAV)
    other = box_client.App(app.binary, ALL_FEATURES, log=app.log, timeout=app.timeout,
                           out=app.out, prefix="%s.run%d" % (app.prefix, run))
    other.start()
    try:
        start = None
        while not os.path.isfile(ALL_FEATURES_WAV):
            frame = other.frame()
            start = frame if start is None else start
            if frame - start > frames:
                other.fail("%s was not written within %d frames" % (ALL_FEATURES_WAV, frames))
        status = parse_value(other.exec("audio"))
        other.expect("logs error", '""')
        warnings = other.exec("logs warning")
        other.check("box.audio" not in warnings, "the scene logged %s" % warnings)
        other.quit()
    finally:
        other.kill()
    with open(ALL_FEATURES_WAV, "rb") as f:
        data = f.read()
    os.unlink(ALL_FEATURES_WAV)
    if app.out:
        with open("%s/%s.run%d.wav" % (app.out, app.prefix, run), "wb") as f:
            f.write(data)
    return data, status


def channels_of(app, data):
    """The left and the right channel of a 16 bit stereo WAV at RATE."""
    with wave.open(io.BytesIO(data)) as w:
        app.check(w.getnchannels() == 2 and w.getframerate() == RATE and
                  w.getsampwidth() == 2, "the capture is %d channels of %d bytes at %d Hz" %
                  (w.getnchannels(), w.getsampwidth(), w.getframerate()))
        count = w.getnframes()
        raw = w.readframes(count)
    samples = struct.unpack("<%dh" % (count * 2), raw)
    return [v / 32767.0 for v in samples[0::2]], [v / 32767.0 for v in samples[1::2]]


def window(x, start, end):
    return x[int(start * RATE):int(end * RATE)]


def rms(x, start, end):
    w = window(x, start, end)
    return math.sqrt(sum(v * v for v in w) / len(w)) if w else 0.0


def peak_of(x, start, end):
    return max((abs(v) for v in window(x, start, end)), default=0.0)


def test_all_features(app):
    first, status = all_features_run(app, 1)
    for bus in ("room", "world", "ui", "music"):
        app.check("%s:" % bus in status, "audio does not show the bus %s: %r" % (bus, status))
    left, right = channels_of(app, first)
    app.check(len(left) == int(ALL_FEATURES_SECONDS * RATE),
              "the capture has %d frames" % len(left))

    # Nothing plays before the click at 0.25 s
    quiet = max(peak_of(left, 0, 0.2), peak_of(right, 0, 0.2))
    app.check(quiet < 1e-3, "the capture peaks at %.4f before the click" % quiet)
    click = max(peak_of(left, 0.25, 0.32), peak_of(right, 0.25, 0.32))
    app.check(click > 0.05, "the click peaks at %.4f" % click)

    # The click ends after 30 ms; what rings on in both channels is the reverb of room, decaying
    for name, x in (("left", left), ("right", right)):
        early, late = rms(x, 0.4, 0.6), rms(x, 0.75, 0.95)
        app.check(early > 3e-4, "no reverb tail on the %s: %.6f after the click" %
                  (name, early))
        app.check(late < early, "the tail on the %s does not decay: %.6f, then %.6f" %
                  (name, early, late))

    # From 1 s the engine on the left, the music in the center and the clicks and the chime
    for name, x in (("left", left), ("right", right)):
        level = rms(x, 1.5, 3.0)
        app.check(level > 0.01, "the %s channel is at %.4f with every source" % (name, level))
    lr, rr = rms(left, 1.1, 1.45), rms(right, 1.1, 1.45)
    app.check(lr > rr * 1.1, "the engine at x -4 is not on the left: %.4f left, %.4f right" %
              (lr, rr))
    # The chime of play_at at x 3, from 1.5 s for 0.25 s, on the right
    lc, rc = rms(left, 1.55, 1.7), rms(right, 1.55, 1.7)
    app.check(rc > lc * 1.3 and rc > rr * 2, "the chime at x 3 is not on the right: %.4f left, "
              "%.4f right" % (lc, rc))
    total = max(peak_of(left, 0, 3), peak_of(right, 0, 3))
    app.check(total < 0.96, "the capture peaks at %.4f, over the limiter" % total)

    second, _ = all_features_run(app, 2)
    app.check(first == second, "two runs wrote different captures (%d and %d bytes, first "
              "difference at byte %s)" % (len(first), len(second), next(
                  (i for i, (a, b) in enumerate(zip(first, second)) if a != b), "none")))
