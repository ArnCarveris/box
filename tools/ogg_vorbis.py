#!/usr/bin/env python3
"""A small lossy Ogg Vorbis encoder for the music that box renders with its own synth.

Usage: python3 tools/ogg_vorbis.py <in.wav> <out.ogg> [--max-bytes N]

There is no Vorbis encoder on the machines that build box. test/assets/audio/make_ogg.py writes
the near lossless test tones with one of its own; this one is the same stream layout with a
codebook that makes music small enough to embed with etc:

- mono, one mode of short blocks of 256 frames (both block sizes 256), the window of the spec;
- floor 1 without partitions, a flat floor whose level is the step of the quantizer;
- residue 1 in partitions of 16 bins with two classes, silent or coded;
- a residue codebook of 2047 entries, the integers -1023..1023, whose lengths grow with the
  magnitude: 0 takes 1 bit and a magnitude of 2^(g-1) to 2^g - 1 takes 2g + 1 bits (2g for the
  last group), a complete prefix code;
- a quantizer with a dead zone, whose step is the coarsest that keeps the file under the limit
  (a step of 1/1023 of the largest coefficient at best).

The input is a 16 bit PCM WAV, mono or stereo (mixed down to mono), at any rate. A signal of
whole loops decodes as a seamless loop: the granule positions are exact, so a decoder returns
exactly the frames of the input.
"""
import math
import operator
import os
import struct
import sys
import wave

BLOCK = 256
HALF = BLOCK // 2
PARTITION = 16
GROUPS = 10
LIMIT = (1 << GROUPS) - 1
ENTRIES = 2 * LIMIT + 1
VALUE_BITS = 11
DEAD_ZONE = 0.62
MAX_BYTES = 900 * 1024


class Bits:
    """Packs bits the way Vorbis reads them: the first bit is the lowest of the first byte."""

    def __init__(self):
        self.out = bytearray()
        self.acc = 0
        self.n = 0

    def put(self, value, bits):
        self.acc |= (value & ((1 << bits) - 1)) << self.n
        self.n += bits
        while self.n >= 8:
            self.out.append(self.acc & 0xFF)
            self.acc >>= 8
            self.n -= 8

    def text(self, data):
        for b in data:
            self.put(b, 8)

    def bytes(self):
        out = bytearray(self.out)
        if self.n:
            out.append(self.acc & 0xFF)
        return bytes(out)


def group(v):
    """The magnitude group of a residue value: 0 for 0, g for 2^(g-1) <= |v| < 2^g."""
    return abs(v).bit_length()


def length(v):
    g = group(v)
    if g == 0:
        return 1
    return 2 * g if g == GROUPS else 2 * g + 1


def codewords(lengths):
    """The codewords a decoder assigns to the lengths of a codebook in entry order, the
    algorithm of the spec (and of stb_vorbis): each entry takes the lowest free codeword of its
    length. The result is the codeword with its first bit highest."""
    available = [0] * 33
    codes = []
    for i, n in enumerate(lengths):
        if i == 0:
            codes.append(0)
            for d in range(1, n + 1):
                available[d] = 1 << (32 - d)
            continue
        z = n
        while z > 0 and not available[z]:
            z -= 1
        assert z > 0, "the lengths of the codebook overflow the tree"
        res = available[z]
        available[z] = 0
        codes.append(res >> (32 - n))
        for y in range(n, z, -1):
            available[y] = res + (1 << (32 - y))
    return codes


def reverse(code, n):
    """A codeword in the order Bits.put writes it, so its first bit is read first."""
    out = 0
    for _ in range(n):
        out = (out << 1) | (code & 1)
        code >>= 1
    return out


LENGTHS = [length(i - LIMIT) for i in range(ENTRIES)]
CODES = [reverse(c, n) for c, n in zip(codewords(LENGTHS), LENGTHS)]


def vorbis_float(x):
    """The float32 of a codebook: a 21 bit mantissa and an exponent biased by 788."""
    sign = 0x80000000 if x < 0 else 0
    mantissa = int(abs(x))
    exponent = 0
    while mantissa >= (1 << 21):
        mantissa >>= 1
        exponent += 1
    return sign | ((exponent + 788) << 21) | mantissa


def identification(rate):
    b = Bits()
    b.put(1, 8)
    b.text(b"vorbis")
    b.put(0, 32)
    b.put(1, 8)
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
    vendor = b"box tools/ogg_vorbis.py"
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
    # Book 1, the residue: entry i is the integer i - LIMIT
    b.put(0x564342, 24)
    b.put(1, 16)
    b.put(ENTRIES, 24)
    b.put(0, 1)
    b.put(0, 1)
    for n in LENGTHS:
        b.put(n - 1, 5)
    b.put(1, 4)
    b.put(vorbis_float(-LIMIT), 32)
    b.put(vorbis_float(1), 32)
    b.put(VALUE_BITS - 1, 4)
    b.put(0, 1)
    for i in range(ENTRIES):
        b.put(i, VALUE_BITS)
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
    the decoder returns frames from 0 on. Scaled by 4 / BLOCK, which the inverse of the
    decoders, without a factor, undoes."""
    w = window()
    blocks = (len(signal) + HALF - 1) // HALF + 1
    scale = 4.0 / BLOCK
    basis = [[scale * math.cos(math.pi / (2 * BLOCK) * (2 * i + 1 + HALF) * (2 * k + 1))
              for i in range(BLOCK)] for k in range(HALF)]
    padded = [0.0] * HALF + list(signal) + [0.0] * (BLOCK + HALF)
    result = []
    mul = operator.mul
    for b in range(blocks):
        frame = list(map(mul, w, padded[b * HALF:b * HALF + BLOCK]))
        result.append([sum(map(mul, frame, row)) for row in basis])
    return result


def quantize(coefficients, step):
    """The residue values of every block, with the dead zone of the quantizer."""
    inv = 1.0 / step
    out = []
    for block in coefficients:
        values = []
        for x in block:
            q = x * inv
            if -DEAD_ZONE < q < DEAD_ZONE:
                values.append(0)
            else:
                values.append(max(-LIMIT, min(LIMIT, int(round(q)))))
        out.append(values)
    return out


def bits_of(blocks):
    """The bits of the audio packets of quantized blocks, without packing them."""
    total = 0
    for values in blocks:
        total += 18
        for p in range(0, HALF, PARTITION):
            part = values[p:p + PARTITION]
            total += 1
            if any(part):
                total += sum(LENGTHS[v + LIMIT] for v in part)
    return total


def audio_packets(blocks, y):
    packets = []
    for values in blocks:
        b = Bits()
        b.put(0, 1)
        b.put(1, 1)
        b.put(y, 8)
        b.put(y, 8)
        for p in range(0, HALF, PARTITION):
            part = values[p:p + PARTITION]
            coded = any(part)
            b.put(1 if coded else 0, 1)
            if coded:
                for v in part:
                    b.put(CODES[v + LIMIT], LENGTHS[v + LIMIT])
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


def encode(signal, rate, max_bytes=MAX_BYTES, serial=0x626f7810):
    """The bytes of an Ogg Vorbis file of a mono signal of floats in -1..1, with the finest
    step that fits in max_bytes. Returns (bytes, step, the noise floor in dB of the peak)."""
    coefficients = spectra(signal)
    peak = max(abs(x) for block in coefficients for x in block) or 1.0
    y = 0
    while y < 255 and peak / floor_level(y) > LIMIT:
        y += 1
    while True:
        blocks = quantize(coefficients, floor_level(y))
        if bits_of(blocks) // 8 + 8192 <= max_bytes or y >= 255:
            break
        y += 2
    packets = audio_packets(blocks, y)
    pages = [page([identification(rate)], 0, 0, 2, serial),
             page([comment(), setup()], 0, 1, 0, serial)]
    group_ = []
    for k, p in enumerate(packets):
        group_.append(p)
        last = k == len(packets) - 1
        size = sum(len(g) // 255 + 1 for g in group_)
        if last or size > 200 or sum(len(g) for g in group_) > 4000:
            granule = len(signal) if last else k * HALF
            pages.append(page(group_, granule, len(pages), 4 if last else 0, serial))
            group_ = []
    data = b"".join(pages)
    return data, floor_level(y), 20 * math.log10(floor_level(y) / peak)


def read_wav(path):
    """The frames of a 16 bit PCM WAV mixed to mono, and its rate."""
    with wave.open(path) as w:
        if w.getsampwidth() != 2:
            raise ValueError("%s is not a 16 bit WAV" % path)
        channels, rate, count = w.getnchannels(), w.getframerate(), w.getnframes()
        raw = w.readframes(count)
    samples = struct.unpack("<%dh" % (count * channels), raw)
    mono = [sum(samples[i * channels:(i + 1) * channels]) / (32767.0 * channels)
            for i in range(count)]
    return mono, rate


def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    max_bytes = MAX_BYTES
    if "--max-bytes" in argv:
        max_bytes = int(argv[argv.index("--max-bytes") + 1])
        args.remove(str(max_bytes))
    if len(args) != 2:
        sys.exit(__doc__)
    signal, rate = read_wav(args[0])
    data, step, noise = encode(signal, rate, max_bytes)
    with open(args[1], "wb") as f:
        f.write(data)
    print("%s: %d frames at %d Hz, %d bytes, noise floor %.1f dB" % (
        os.path.basename(args[1]), len(signal), rate, len(data), noise))


if __name__ == "__main__":
    main(sys.argv[1:])
