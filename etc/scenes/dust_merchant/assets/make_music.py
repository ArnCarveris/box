#!/usr/bin/env python3
"""Renders the music of the dust merchant scene with the synth of box and encodes it to Ogg.

Usage: python3 etc/scenes/dust_merchant/assets/make_music.py <boxrun binary>

music_score.flecs next to this script is the score. The script runs it in a headless boxrun with
a fixed delta of 1/60 s, which renders the null transport at 48000 Hz and writes the master to a
WAV with box.audio.capture, the same in every run. The score plays 32 seconds once and the
capture keeps its tail; the tail is added to the start, which makes a seamless loop of exactly 32
seconds whose first notes ring with the reverb of the last ones. The loop is normalized to a
peak of 0.7, mixed to mono and encoded with tools/ogg_vorbis.py into dust_merchant_loop.ogg,
under 1 MB since the web build embeds etc. No third party audio is involved.

The file is checked in; run the script again after a change of the score and commit what it
writes.
"""
import os
import struct
import subprocess
import sys
import tempfile
import time
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import ogg_vorbis  # noqa: E402

SCORE = os.path.join(HERE, "music_score.flecs")
OUT = os.path.join(HERE, "dust_merchant_loop.ogg")
RATE = 48000
LOOP_SECONDS = 32
CAPTURE_SECONDS = 36
DELTA_TIME = "0.016666667"
PEAK = 0.7
MAX_BYTES = 1000 * 1000


def render(binary, wav):
    """Run the score headless and wait for the WAV of its capture."""
    with open(SCORE) as f:
        score = f.read()
    folder = os.path.dirname(wav)
    scene = os.path.join(folder, "music_score.flecs")
    with open(scene, "w") as f:
        f.write(score)
        f.write('\nconst captured: bool = box.audio.capture(%d, "%s")\n' % (CAPTURE_SECONDS,
                                                                            wav))
    frames = int(CAPTURE_SECONDS * 60) + 120
    cmd = [binary, "--headless", "--delta-time", DELTA_TIME, "--frames", str(frames), scene]
    run = subprocess.run(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         timeout=600)
    deadline = time.time() + 10
    while not os.path.isfile(wav) and time.time() < deadline:
        time.sleep(0.1)
    if not os.path.isfile(wav):
        sys.exit("the capture was not written:\n%s" % run.stdout.decode(errors="replace"))
    log = run.stdout.decode(errors="replace")
    if "error" in log.lower():
        print(log)


def read(wav):
    with wave.open(wav) as w:
        channels, rate, count = w.getnchannels(), w.getframerate(), w.getnframes()
        raw = w.readframes(count)
    if channels != 2 or rate != RATE:
        sys.exit("the capture is %d channels at %d Hz" % (channels, rate))
    s = struct.unpack("<%dh" % (count * 2), raw)
    return [(s[2 * i] + s[2 * i + 1]) / (2 * 32767.0) for i in range(count)]


def loop(x):
    """The loop from the first note on, with the tail after it added to its start."""
    start = next((i for i, v in enumerate(x) if abs(v) > 1e-5), None)
    if start is None:
        sys.exit("the capture is silent")
    n = LOOP_SECONDS * RATE
    out = []
    for i in range(n):
        a = start + i
        b = a + n
        out.append(x[a] if a < len(x) else 0.0)
        if b < len(x):
            out[-1] += x[b]
    peak = max(abs(v) for v in out) or 1.0
    return [v * PEAK / peak for v in out], start, peak


def main(argv):
    if len(argv) != 1:
        sys.exit(__doc__)
    binary = os.path.abspath(argv[0])
    with tempfile.TemporaryDirectory() as tmp:
        wav = os.path.join(tmp, "music.wav")
        render(binary, wav)
        x = read(wav)
    signal, start, peak = loop(x)
    data, step, noise = ogg_vorbis.encode(signal, RATE, MAX_BYTES - 50 * 1000)
    with open(OUT, "wb") as f:
        f.write(data)
    print("%s: %.1f s from frame %d, peak %.3f before the normalize, %d bytes, noise floor "
          "%.1f dB" % (os.path.relpath(OUT, ROOT), len(signal) / RATE, start, peak, len(data),
                       noise))
    if len(data) >= MAX_BYTES:
        sys.exit("the track is over 1 MB")


if __name__ == "__main__":
    main(sys.argv[1:])
