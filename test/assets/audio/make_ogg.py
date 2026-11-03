#!/usr/bin/env python3
"""Writes the Ogg Vorbis files of the Stream tests (test/cpu/audio) and of the browser check
test/integration/web/audio_stream.mjs into this folder.

Usage: python3 test/assets/audio/make_ogg.py

The signals are sines the tests compute again. There is no Vorbis encoder on the machines that
build box, so this script is a small one of its own, enough for a known tone:

- one mode of short blocks of 256 frames (both block sizes 256), the window of the spec;
- floor 1 without partitions, a flat floor whose level is the step of the quantizer;
- residue 1 in partitions of 16 bins with two classes, silent or coded, and a codebook of
  4096 entries of 12 bits that holds the integers -2048..2047;
- the forward MDCT that the inverse MDCT of the decoders inverts, so the quantizer is the only
  loss: a sample is within about 1e-3 of the tone.

The granule positions are exact: packet k ends at sample k * 128 and the last page ends at the
length of the tone, so a decoder returns exactly the frames of the tone and a loop of a tone of
whole periods has no seam. corrupt.ogg is an Ogg page that holds no Vorbis stream.

The files are checked in; run the script again after a change and commit what it writes.
"""
import math
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))

BLOCK = 256
HALF = BLOCK // 2
PARTITION = 16
ENTRIES = 4096
BITS = 12
LOW = -ENTRIES // 2
HIGH = ENTRIES // 2 - 1


class Bits:
    """Packs bits the way Vorbis reads them: the first bit is the lowest of the first byte."""

    def __init__(self):
        self.out = bytearray()
        self.acc = 0
        self.n = 0

    def put(self, value, bits):
        for i in range(bits):
            self.acc |= ((value >> i) & 1) << self.n
            self.n += 1
            if self.n == 8:
                self.out.append(self.acc)
                self.acc = 0
                self.n = 0

    def code(self, codeword, length):
        """A Huffman codeword, its highest bit first."""
        for i in range(length - 1, -1, -1):
            self.put((codeword >> i) & 1, 1)

    def text(self, data):
        for b in data:
            self.put(b, 8)

    def bytes(self):
        out = bytearray(self.out)
        if self.n:
            out.append(self.acc)
        return bytes(out)


def vorbis_float(x):
    """The float32 of a codebook: a 21 bit mantissa and an exponent biased by 788."""
    sign = 0x80000000 if x < 0 else 0
    mantissa = int(abs(x))
    exponent = 0
    while mantissa >= (1 << 21):
        mantissa >>= 1
        exponent += 1
    return sign | ((exponent + 788) << 21) | mantissa


def identification(channels, rate):
    b = Bits()
    b.put(1, 8)
    b.text(b"vorbis")
    b.put(0, 32)
    b.put(channels, 8)
    b.put(rate, 32)
    b.put(0, 32)
    b.put(0, 32)
    b.put(0, 32)
    exponent = BLOCK.bit_length() - 1
    b.put(exponent, 4)
    b.put(exponent, 4)
    b.put(1, 1)
    return b.bytes()


def comment():
    vendor = b"box make_ogg.py"
    b = Bits()
    b.put(3, 8)
    b.text(b"vorbis")
    b.put(len(vendor), 32)
    b.text(vendor)
    b.put(0, 32)
    b.put(1, 1)
    return b.bytes()


def setup():
    b = Bits()
    b.put(5, 8)
    b.text(b"vorbis")
    b.put(2 - 1, 8)
    # Book 0, the classes of the partitions: 0 is silent, 1 coded
    b.put(0x564342, 24)
    b.put(1, 16)
    b.put(2, 24)
    b.put(0, 1)
    b.put(0, 1)
    b.put(0, 5)
    b.put(0, 5)
    b.put(0, 4)
    # Book 1, the residue: entry i is the integer LOW + i
    b.put(0x564342, 24)
    b.put(1, 16)
    b.put(ENTRIES, 24)
    b.put(0, 1)
    b.put(0, 1)
    for _ in range(ENTRIES):
        b.put(BITS - 1, 5)
    b.put(1, 4)
    b.put(vorbis_float(LOW), 32)
    b.put(vorbis_float(1), 32)
    b.put(BITS - 1, 4)
    b.put(0, 1)
    for i in range(ENTRIES):
        b.put(i, BITS)
    # Time domain transforms, unused
    b.put(0, 6)
    b.put(0, 16)
    # Floor 1 without partitions over the bins of a block
    b.put(0, 6)
    b.put(1, 16)
    b.put(0, 5)
    b.put(0, 2)
    b.put(HALF.bit_length() - 1, 4)
    # Residue 1
    b.put(0, 6)
    b.put(1, 16)
    b.put(0, 24)
    b.put(HALF, 24)
    b.put(PARTITION - 1, 24)
    b.put(2 - 1, 6)
    b.put(0, 8)
    b.put(0, 3)
    b.put(0, 1)
    b.put(1, 3)
    b.put(0, 1)
    b.put(1, 8)
    # Mapping 0: one submap, no coupling
    b.put(0, 6)
    b.put(0, 16)
    b.put(0, 1)
    b.put(0, 1)
    b.put(0, 2)
    b.put(0, 8)
    b.put(0, 8)
    b.put(0, 8)
    # One mode of short blocks
    b.put(0, 6)
    b.put(0, 1)
    b.put(0, 16)
    b.put(0, 16)
    b.put(0, 8)
    b.put(1, 1)
    return b.bytes()


def window():
    return [math.sin(0.5 * math.pi * math.sin((i + 0.5) / BLOCK * math.pi) ** 2)
            for i in range(BLOCK)]


def floor_level(y):
    """The floor of a Y value with multiplier 1, the inverse dB table of the spec."""
    return 10.0 ** (7.0 * (y + 1) / 256.0 - 7.0)


def spectra(signal):
    """The MDCT of every block: block k covers the frames (k - 1) * HALF .. (k + 1) * HALF, so
    the decoder returns frames from 0 on. Scaled by 2 / BLOCK, which the inverse of the
    decoders, without a factor, undoes."""
    w = window()
    blocks = (len(signal) + HALF - 1) // HALF + 1
    basis = [[math.cos(math.pi / (2 * BLOCK) * (2 * i + 1 + HALF) * (2 * k + 1))
              for i in range(BLOCK)] for k in range(HALF)]
    result = []
    for b in range(blocks):
        start = (b - 1) * HALF
        frame = [w[i] * (signal[start + i] if 0 <= start + i < len(signal) else 0.0)
                 for i in range(BLOCK)]
        result.append([2.0 / BLOCK * sum(a * c for a, c in zip(frame, row)) for row in basis])
    return result


def audio_packets(signal):
    coefficients = spectra(signal)
    peak = max(abs(x) for block in coefficients for x in block)
    y = 0
    while y < 255 and peak / floor_level(y) > HIGH:
        y += 1
    step = floor_level(y)
    packets = []
    for block in coefficients:
        b = Bits()
        b.put(0, 1)
        b.put(1, 1)
        b.put(y, 8)
        b.put(y, 8)
        for p in range(HALF // PARTITION):
            values = [max(LOW, min(HIGH, int(round(x / step))))
                      for x in block[p * PARTITION:(p + 1) * PARTITION]]
            coded = any(values)
            b.code(1 if coded else 0, 1)
            if coded:
                for v in values:
                    b.code(v - LOW, BITS)
        packets.append(b.bytes())
    return packets


def crc32(data):
    crc = 0
    for byte in data:
        crc ^= byte << 24
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if crc & 0x80000000 else \
                (crc << 1) & 0xFFFFFFFF
    return crc


def page(packets, granule, sequence, flags, serial):
    lacing = bytearray()
    for p in packets:
        lacing += bytes([255] * (len(p) // 255) + [len(p) % 255])
    assert len(lacing) <= 255
    header = b"OggS" + struct.pack("<BBqIIIB", 0, flags, granule, serial, sequence, 0,
                                   len(lacing))
    body = header + bytes(lacing) + b"".join(packets)
    crc = crc32(body)
    return body[:22] + struct.pack("<I", crc) + body[26:]


def ogg(name, signal, rate, serial):
    packets = audio_packets(signal)
    pages = [page([identification(1, rate)], 0, 0, 2, serial),
             page([comment(), setup()], 0, 1, 0, serial)]
    group = []
    for k, p in enumerate(packets):
        group.append(p)
        last = k == len(packets) - 1
        size = sum(len(g) // 255 + 1 for g in group)
        if last or size > 200 or sum(len(g) for g in group) > 4000:
            granule = len(signal) if last else k * HALF
            pages.append(page(group, granule, len(pages), 4 if last else 0, serial))
            group = []
    with open(os.path.join(HERE, name), "wb") as f:
        f.write(b"".join(pages))


def sine(frequency, rate, frames, level):
    return [level * math.sin(2 * math.pi * frequency * i / rate) for i in range(frames)]


def main():
    # 120 whole periods of 480 Hz in 0.25 s, so the loop has no seam
    ogg("tone.ogg", sine(480, 48000, 12000, 0.5), 48000, 0x626f7801)
    # 120 periods of 960 Hz in 0.125 s, the other path of change_path
    ogg("tone_960.ogg", sine(960, 48000, 6000, 0.25), 48000, 0x626f7802)
    # An Ogg page whose packet is no Vorbis header
    junk = bytes((i * 37 + 11) & 0xFF for i in range(30))
    with open(os.path.join(HERE, "corrupt.ogg"), "wb") as f:
        f.write(page([junk], 0, 0, 2, 0x626f7803) + bytes(range(256)) * 4)


if __name__ == "__main__":
    main()
