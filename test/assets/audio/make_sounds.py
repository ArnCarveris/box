#!/usr/bin/env python3
"""Writes the sound files of the Sample and Analyze tests (test/cpu/audio), of the audio analyze
command (test/cpu/command) and of the browser check
test/integration/web/audio_sample.mjs, the drum and the shaker of song.flecs, and the wavetable of the
SynthWavetableAsset tests, into this folder.

Usage: python3 test/assets/audio/make_sounds.py

The signals are the formulas the tests compute again: sines at a peak, constants per channel.
16 bit samples are round(x * 32767). The FLAC files use verbatim subframes, which every decoder
reads, so the script needs no encoder. The files are checked in; run the script again after a
change and commit what it writes.
"""
import math
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))


def sine(frequency, rate, frames, level, phase=0.0):
    return [level * math.sin(2 * math.pi * frequency * i / rate + phase) for i in range(frames)]


def s16(x):
    return max(-32768, min(32767, int(round(x * 32767))))


def kick(rate, frames):
    """A kick drum: a sine that sweeps from 150 to 45 Hz over 0.1 s and decays."""
    out, phase = [], 0.0
    for i in range(frames):
        t = i / rate
        phase += 2 * math.pi * (45 + 105 * math.exp(-t / 0.03)) / rate
        out.append(0.8 * math.exp(-t / 0.12) * math.sin(phase))
    return out


def shaker(rate, frames, steps):
    """A shaker: bursts of noise on steps of equal length, every other one softer. The noise
    is a linear congruential generator, so every run writes the same samples."""
    out, seed, step = [], 1, frames // steps
    for i in range(frames):
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
        t = (i % step) / rate
        level = 0.5 if (i // step) % 2 == 0 else 0.25
        out.append(level * math.exp(-t / 0.025) * (seed / 0x3FFFFFFF - 1))
    return out


def saw_square(frames, size, harmonics, level):
    """A wavetable of single cycle frames of size samples that morphs from a saw at frame 0 to
    a square at the last frame, each frame a linear mix of the two, band limited to a number
    of harmonics: the saw is the sum of -2 / (pi n) sin(n x), the square the sum of
    4 / (pi n) sin(n x) over the odd n, as the saw and the square of WavetableBasic."""
    out = []
    for j in range(frames):
        m = j / (frames - 1)
        amps = []
        for n in range(1, harmonics + 1):
            saw = -2 / (math.pi * n)
            square = 4 / (math.pi * n) if n % 2 else 0.0
            amps.append(level * ((1 - m) * saw + m * square))
        for t in range(size):
            x = 2 * math.pi * t / size
            out.append(sum(a * math.sin((n + 1) * x) for n, a in enumerate(amps)))
    return out


def pink(rate, frames, rms):
    """Pink noise at an RMS level: white noise of a linear congruential generator through the
    filter of Paul Kellet, scaled so the RMS is exact."""
    out, seed, b = [], 1, [0.0] * 7
    for _ in range(frames):
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
        w = seed / 0x3FFFFFFF - 1
        b[0] = 0.99886 * b[0] + w * 0.0555179
        b[1] = 0.99332 * b[1] + w * 0.0750759
        b[2] = 0.96900 * b[2] + w * 0.1538520
        b[3] = 0.86650 * b[3] + w * 0.3104856
        b[4] = 0.55000 * b[4] + w * 0.5329522
        b[5] = -0.7616 * b[5] - w * 0.0168980
        out.append(sum(b) + w * 0.5362)
        b[6] = w * 0.115926
    scale = rms / math.sqrt(sum(x * x for x in out) / frames)
    return [x * scale for x in out]


def loudness_48k(channels):
    """Ungated loudness in LUFS of channels at 48000 Hz, with the K-weighting coefficients that
    ITU-R BS.1770 lists for 48 kHz, as the expected value of the Analyze tests."""
    stages = [((1.53512485958697, -2.69169618940638, 1.19839281085285),
               (-1.69065929318241, 0.73248077421585)),
              ((1.0, -2.0, 1.0), (-1.99004745483398, 0.99007225036621))]
    power = 0.0
    for c in channels:
        x = list(c)
        for (b0, b1, b2), (a1, a2) in stages:
            y, x1, x2, y1, y2 = [], 0.0, 0.0, 0.0, 0.0
            for v in x:
                o = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
                x2, x1, y2, y1 = x1, v, y1, o
                y.append(o)
            x = y
        power += sum(v * v for v in x) / len(x)
    return -0.691 + 10 * math.log10(power)


def interleave(channels):
    return [c[i] for i in range(len(channels[0])) for c in channels]


def wav(name, rate, channels, float32=False):
    """Writes a PCM 16 bit or IEEE float 32 bit WAV of a list of channels."""
    data = interleave(channels)
    count = len(channels)
    if float32:
        body = struct.pack("<%df" % len(data), *data)
        fmt, bits = 3, 32
    else:
        body = struct.pack("<%dh" % len(data), *[s16(x) for x in data])
        fmt, bits = 1, 16
    align = count * bits // 8
    chunk = struct.pack("<HHIIHH", fmt, count, rate, rate * align, align, bits)
    out = b"RIFF" + struct.pack("<I", 4 + 8 + len(chunk) + 8 + len(body)) + b"WAVE"
    out += b"fmt " + struct.pack("<I", len(chunk)) + chunk
    out += b"data" + struct.pack("<I", len(body)) + body
    with open(os.path.join(HERE, name), "wb") as f:
        f.write(out)


class Bits:
    def __init__(self):
        self.out = bytearray()
        self.acc = 0
        self.n = 0

    def put(self, value, bits):
        for i in range(bits - 1, -1, -1):
            self.acc = (self.acc << 1) | ((value >> i) & 1)
            self.n += 1
            if self.n == 8:
                self.out.append(self.acc)
                self.acc = 0
                self.n = 0

    def align(self):
        while self.n:
            self.put(0, 1)


def crc8(data):
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def crc16(data):
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x8005) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def utf8(n):
    if n < 0x80:
        return bytes([n])
    out = []
    limit = 0x3F
    while n > limit:
        out.insert(0, 0x80 | (n & 0x3F))
        n >>= 6
        limit >>= 1
    lead = (0xFF << (7 - len(out))) & 0xFF
    return bytes([lead | n]) + bytes(out)


RATE_CODES = {88200: 1, 176400: 2, 192000: 3, 8000: 4, 16000: 5, 22050: 6, 24000: 7,
              32000: 8, 44100: 9, 48000: 10, 96000: 11}


def flac(name, rate, channels, block=4096):
    """Writes a 16 bit FLAC of a list of channels, with verbatim subframes."""
    frames = len(channels[0])
    samples = [[s16(x) for x in c] for c in channels]
    out = bytearray(b"fLaC")
    info = Bits()
    info.put(block, 16)
    info.put(block, 16)
    info.put(0, 24)
    info.put(0, 24)
    info.put(rate, 20)
    info.put(len(channels) - 1, 3)
    info.put(15, 5)
    info.put(frames, 36)
    info.put(0, 64)
    info.put(0, 64)
    out += bytes([0x80]) + struct.pack(">I", len(info.out))[1:] + info.out
    for number, start in enumerate(range(0, frames, block)):
        size = min(block, frames - start)
        head = Bits()
        head.put(0x3FFE, 14)
        head.put(0, 1)
        head.put(0, 1)
        head.put(7, 4)
        head.put(RATE_CODES[rate], 4)
        head.put(len(channels) - 1, 4)
        head.put(4, 3)
        head.put(0, 1)
        header = bytes(head.out) + utf8(number) + struct.pack(">H", size - 1)
        header += bytes([crc8(header)])
        body = Bits()
        for c in samples:
            body.put(0, 1)
            body.put(1, 6)
            body.put(0, 1)
            for x in c[start:start + size]:
                body.put(x & 0xFFFF, 16)
        body.align()
        frame = header + bytes(body.out)
        out += frame + struct.pack(">H", crc16(frame))
    with open(os.path.join(HERE, name), "wb") as f:
        f.write(out)


def analyze_sounds():
    """The recordings of the Analyze tests and of the audio analyze command."""
    rate = 48000
    # A full scale 1000 Hz sine, mono, 2 s: -3.01 LUFS, 0 dBFS, all in the mid band
    wav("analyze_sine_1k.wav", rate, [sine(1000, rate, 2 * rate, 1.0)])
    # Pink noise at -20 dBFS RMS, 4 s, as floats
    noise = [struct.unpack("<f", struct.pack("<f", x))[0] for x in pink(rate, 4 * rate, 0.1)]
    wav("analyze_pink.wav", rate, [noise], float32=True)
    print("analyze_pink.wav: %.2f LUFS" % loudness_48k([noise]))
    # A 440 Hz sine at 0.5 on the left and its inverted copy on the right, 1 s
    tone = sine(440, rate, rate, 0.5)
    wav("analyze_inverted.wav", rate, [tone, [-x for x in tone]])
    # A full scale sine at a quarter of the rate with a phase of 45 degrees: its samples peak
    # at -3.01 dBFS, the wave between them at 0 dBFS
    wav("analyze_true_peak.wav", rate, [sine(rate / 4, rate, rate, 1.0, math.pi / 4)],
        float32=True)
    # A 440 Hz tone at 0.8 that decays with a time constant of 0.2 s, 3 s as floats: it falls
    # 60 dB in 0.2 * ln(1000) = 1.3816 s
    wav("analyze_decay.wav", rate, [[0.8 * math.exp(-i / rate / 0.2) *
        math.sin(2 * math.pi * 440 * i / rate) for i in range(3 * rate)]], float32=True)
    # A 440 Hz cosine at 0.5, 0.5 s: its first sample is 0.5, a click
    wav("analyze_click.wav", rate, [sine(440, rate, rate // 2, 0.5, math.pi / 2)])
    # An IMA ADPCM header, a format the analysis does not read
    chunk = struct.pack("<HHIIHH", 0x11, 1, rate, rate // 2, 256, 4)
    body = bytes(256)
    out = b"RIFF" + struct.pack("<I", 4 + 8 + len(chunk) + 8 + len(body)) + b"WAVE"
    out += b"fmt " + struct.pack("<I", len(chunk)) + chunk
    out += b"data" + struct.pack("<I", len(body)) + body
    with open(os.path.join(HERE, "analyze_adpcm.wav"), "wb") as f:
        f.write(out)


def peaking(x, rate, frequency, gain_db, q):
    """A peaking filter of the Audio EQ Cookbook: gain_db at frequency, with a bandwidth of q."""
    a = 10 ** (gain_db / 40)
    w = 2 * math.pi * frequency / rate
    alpha = math.sin(w) / (2 * q)
    b0, b1, b2 = 1 + alpha * a, -2 * math.cos(w), 1 - alpha * a
    a0, a1, a2 = 1 + alpha / a, -2 * math.cos(w), 1 - alpha / a
    out, x1, x2, y1, y2 = [], 0.0, 0.0, 0.0, 0.0
    for v in x:
        o = (b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0
        x2, x1, y2, y1 = x1, v, y1, o
        out.append(o)
    return out


def f32(x):
    return [struct.unpack("<f", struct.pack("<f", v))[0] for v in x]


def diff_sounds():
    """The recordings of the audio diff command: stereo pink noise at -20 dBFS RMS, 1 s, as
    floats, its left channel reversed on the right, and copies of it changed."""
    rate = 48000
    left = pink(rate, rate, 0.1)
    base = [f32(left), f32(left[::-1])]
    wav("diff_base.wav", rate, base, float32=True)
    # 6 dB quieter
    gain = 10 ** (-6 / 20)
    wav("diff_quiet.wav", rate, [f32([v * gain for v in c]) for c in base], float32=True)
    # 10 samples later, of the same length
    wav("diff_delay.wav", rate, [[0.0] * 10 + c[:-10] for c in base], float32=True)
    # A peaking boost of 6 dB at 3 kHz with a q of 1
    wav("diff_boost.wav", rate, [f32(peaking(c, rate, 3000, 6, 1.0)) for c in base],
        float32=True)


def main():
    # Sample/wav_16, mono, flac: a 1000 Hz sine at 0.5, a quarter of a second at 48000 Hz
    tone = sine(1000, 48000, 12000, 0.5)
    wav("sine_16.wav", 48000, [tone])
    flac("sine.flac", 48000, [tone])
    # Sample/wav_float: the same sine as 32 bit floats
    wav("sine_float.wav", 48000, [tone], float32=True)
    # Sample/stereo: 500 Hz at 0.5 on the left, 1000 Hz at 0.25 on the right
    wav("stereo.wav", 48000, [sine(500, 48000, 12000, 0.5), sine(1000, 48000, 12000, 0.25)])
    # Sample/rate_44100_on_48000 and the browser check: one second of 440 Hz at 0.25, 44100 Hz
    wav("tone_44100.wav", 44100, [sine(440, 44100, 44100, 0.25)])
    # Sample/multichannel: 5.1 constants FL 0.4, FR -0.4, FC 0.2, LFE 0.9, BL 0.1, BR -0.1
    levels = [0.4, -0.4, 0.2, 0.9, 0.1, -0.1]
    wav("surround.wav", 48000, [[v] * 4800 for v in levels])
    # Sample/loop_seam: 48 periods of 480 Hz at 0.5, floats, so the loop closes exactly
    wav("loop.wav", 48000, [sine(480, 48000, 4800, 0.5)], float32=True)
    # Sample/corrupt_file and Sample/empty_file
    with open(os.path.join(HERE, "corrupt.wav"), "wb") as f:
        f.write(b"RIFF\x24\x00\x00\x00WAVEfmt \x10\x00\x00\x00" + bytes(range(64)))
    with open(os.path.join(HERE, "empty.wav"), "wb") as f:
        pass
    # The stall the decode of 2 seconds of stereo costs in the browser check
    long_stereo = [sine(440, 48000, 96000, 0.25), sine(660, 48000, 96000, 0.25)]
    wav("long_stereo.wav", 48000, long_stereo)
    flac("long_stereo.flac", 48000, long_stereo)
    # The sampled drum and the audio clip of song.flecs, the song of the guide: a kick of 0.4 s
    # and a shaker of 8 sixteenths, 2 beats at 120 bpm
    wav("kick.wav", 48000, [kick(48000, 19200)])
    wav("shaker.wav", 48000, [shaker(48000, 48000, 8)])
    # SynthWavetableAsset: 8 frames of 2048 samples from a saw to a square, 64 harmonics at a
    # level of 0.5, as floats, which the table normalizes to a peak of 1
    wav("wavetable_saw_square.wav", 48000, [saw_square(8, 2048, 64, 0.5)], float32=True)
    analyze_sounds()
    diff_sounds()


if __name__ == "__main__":
    main()
