"""The audio command over the shell endpoints: the status and the buses, the master volume and
mute, and a capture of a known tone to a WAV resource.

Every case runs in a fresh headless apps/boxrun process with test/assets/audio/tone.flecs (named
from the repository root): a looping 440 Hz tone at a peak of 0.25 on the bus music. Headless
runs open no device and render with the null transport at 48000 Hz, which is not an error: the
runner fails a case on any logged error.
"""
import io
import math
import struct
import wave

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
    app.check("master: volume 1.00" in out, "audio does not show the master: %r" % out)
    app.check("music: volume 1.00 pan 0.00 -> master" in out,
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
