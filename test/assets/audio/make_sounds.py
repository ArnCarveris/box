#!/usr/bin/env python3
"""Writes the sound files of the Sample tests (test/cpu/audio) and of the browser check
test/integration/web/audio_sample.mjs into this folder.

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


if __name__ == "__main__":
    main()
