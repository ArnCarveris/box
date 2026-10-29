#!/usr/bin/env python3
"""Re-encodes the world colors of Ore Else so box shows the linear light of the original.

flecs-engine reads a color byte b as the linear value b / 255; box decodes color bytes as sRGB.
A byte the port writes for a byte b of the original is therefore the sRGB encoding of b / 255:
round(255 * srgb_encode(b / 255)), and box decodes it back to the original's linear value
(within half a step of the sRGB byte). See apps/oreelse/README.md (Colors).

The conversion ran once over apps/oreelse/etc. The UI (apps/oreelse/etc/ui and the health bar
colors of etc/config.flecs) went back to the original's bytes afterwards: the original shows its
UI bytes as sRGB, so only the world takes the conversion. Running `apply` again would encode
the colors a second time; it is kept for reference and to show which literals count as colors.
Port a new world color of the original with `encode`, read a world color of the port against the
original with `decode`.

Usage: linear_colors.py encode r g b       the bytes to write for an original color
       linear_colors.py decode r g b       the original color of bytes of the port
       linear_colors.py scan [files...]    list the literals apply would convert, change nothing
       linear_colors.py apply [files...]   convert the literals in place (once!)

Without files, scan and apply go over every .flecs file of apps/oreelse/etc but etc/ui (apply
also ran over apps/oreelse/test/visual). What counts as a
color literal: three integers, or four with an alpha (rgba) or strength (Tint) that stays as it
is, in braces, positional or named r, g, b, a, that are
  - the value of a color component or member: Rgb, Tint, color, and the keys start, early,
    late and end of a ParticleGradient (inside color), the live, dead, warn and ok of
    OreStatusLamp, and the props of the templates declared with a color type (the Light's
    dead, warn and ok, the tints of the deposits and the flora, the astronaut's suit);
  - the value of a constant or prop declared with a color type: Rgb, rgb, rgba, Tint;
  - an arm of a match whose constant is declared with a color type;
  - the color of a positional Emissive: {strength, {r, g, b}}.
The scan also lists the remaining integer triplets and quads whose key it does not know, so
they can be checked by hand. Colors computed in script cannot be converted literal by literal
and were changed by hand: the random tints of the scenery's flora encode the original's bytes
with oreelse.srgb() (src/look.c). The health bar is UI: it blends cfg.hpBarFull, hpBarMid and
hpBarLow, the original's bytes, and shows the blend as it is.

apply also sets `linear: true` on every TexturePath except the normal and ORM maps, so the
albedo and particle images load as the original samples them (TexturePath.linear in
include/box/texture.h); the UI images (the icons, the hazard strip of the panels, the signs)
load without it.
"""
import glob
import math
import os
import re
import sys

APP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Keys whose value is a color
COLOR_KEYS = {
    "Rgb", "Tint", "color", "live", "dead", "warn", "ok", "suit", "tint", "padTint", "edge",
    "fill", "rootTint", "stalkTint", "nubTint", "bulbTint", "podTint", "quillTint"
}

# Keys of a ParticleGradient, colors only inside a color member
GRADIENT_KEYS = {"start", "early", "late", "end"}

# Types a constant or prop is declared with that hold a color
COLOR_TYPES = {"Rgb", "rgb", "rgba", "Tint"}

# Components whose positional members hold a color
POSITIONAL = {"Emissive"}

# Keys whose integer triplets and quads are not colors, left out of the list of unknown ones
NOT_COLORS = {
    "Position", "Rotation", "Scale", "direction", "padding", "Box", "size", "radial", "speed",
    "gravity", "lifetime", "opacity", "spin", "rotation", "min", "max", "offset", "margin"
}

INT_LIST = re.compile(
    r"^\s*(?:r\s*:\s*)?(\d+)\s*,\s*(?:g\s*:\s*)?(\d+)\s*,\s*(?:b\s*:\s*)?(\d+)\s*"
    r"(?:,\s*(?:a\s*:\s*)?(\d+)\s*)?$")
DECL = re.compile(r"(\w+)\s*:\s*(\w+)\s*=\s*$")
MATCH = re.compile(r":\s*(\w+)\s*=\s*match\b[^{}]*$")
KEY = re.compile(r"(\w+)\s*:\s*$")
TEXTURE = re.compile(r"TexturePath:\s*\{([^{}]*)\}")


def srgb_encode(c):
    """sRGB encoding of a linear value from 0 to 1."""
    return c * 12.92 if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055


def srgb_decode(c):
    """Linear value of an sRGB encoded value from 0 to 1."""
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def encode(b):
    """The byte box decodes to the linear value b / 255."""
    return int(math.floor(255 * srgb_encode(b / 255) + 0.5))


def decode(b):
    """The byte of the original whose linear value box decodes the byte b to."""
    return int(math.floor(255 * srgb_decode(b / 255) + 0.5))


class Group:
    """A pair of braces: what precedes it, and whether braces are nested in it."""

    def __init__(self, start, kind, name, parent):
        self.start = start
        self.kind = kind
        self.name = name
        self.parent = parent
        self.nested = False


def context(text):
    """Kind and name of what precedes an opening brace: decl, match, key, pos or other."""
    result = ("other", None)
    m = MATCH.search(text)
    if m:
        result = ("match", m.group(1))
    elif DECL.search(text):
        result = ("decl", DECL.search(text).group(2))
    elif KEY.search(text):
        result = ("key", KEY.search(text).group(1))
    elif re.search(r"[,{]\s*$", text):
        result = ("pos", None)
    return result


def is_color(group):
    """Whether the literal in a group is a color, from its key and its parents."""
    parent = group.parent
    if group.kind == "decl":
        return group.name in COLOR_TYPES
    if group.kind == "key" and group.name in COLOR_KEYS:
        return True
    if group.kind == "key" and group.name in GRADIENT_KEYS:
        return parent is not None and parent.kind == "key" and parent.name == "color"
    if group.kind == "key" and parent is not None and parent.kind == "match":
        return parent.name in COLOR_TYPES
    if group.kind == "pos" and parent is not None and parent.kind == "key":
        return parent.name in POSITIONAL
    return False


def strip(text):
    """The text with its comments and strings blanked out, so offsets stay the same."""
    def blank(m):
        return re.sub(r"[^\n]", " ", m.group(0))
    return re.sub(r'//[^\n]*|"(?:\\.|[^"\\])*"', blank, text)


def literals(text):
    """Find the integer literals in braces: (start, end, values, group, is_color) each."""
    code = strip(text)
    stack = []
    result = []
    for i, ch in enumerate(code):
        if ch == "{":
            before = code[max(0, i - 200):i]
            kind, name = context(before)
            if stack:
                stack[-1].nested = True
            stack.append(Group(i, kind, name, stack[-1] if stack else None))
        elif ch == "}" and stack:
            group = stack.pop()
            if group.nested:
                continue
            m = INT_LIST.match(code[group.start + 1:i])
            if m:
                result.append((group.start, i + 1, m, group, is_color(group)))
    return result


def convert(text, m, start):
    """The literal of a match with its color bytes encoded, keeping names and spacing."""
    body = text[start + 1:start + 1 + len(m.group(0))]
    out = []
    last = 0
    for index in (1, 2, 3):
        s, e = m.span(index)
        out.append(body[last:s])
        out.append(str(encode(int(m.group(index)))))
        last = e
    out.append(body[last:])
    return "{" + "".join(out) + "}"


def line_of(text, offset):
    """Line number of an offset."""
    return text.count("\n", 0, offset) + 1


def textures(text):
    """The text with linear: true added to every TexturePath of a color image."""
    def add(m):
        body = m.group(1)
        if re.search(r"_(normal|orm|rough)\.svg", body) or "linear:" in body:
            return m.group(0)
        return "TexturePath: {" + body.rstrip() + ", linear: true}"
    return TEXTURE.sub(add, text)


def process(path, apply):
    """Scan or convert one file; return the number of colors and the unknown literals."""
    with open(path) as f:
        text = f.read()
    found = literals(text)
    colors = [x for x in found if x[4]]
    unknown = [x for x in found
               if not x[4] and not (x[3].kind == "key" and x[3].name in NOT_COLORS)]
    rel = os.path.relpath(path, APP)
    for start, end, m, group, _ in colors:
        print("%s:%d: %s %s -> %s" % (rel, line_of(text, start), group.name,
            text[start:end], convert(text, m, start)))
    for start, end, m, group, _ in unknown:
        print("%s:%d: unknown (%s %s) %s" % (rel, line_of(text, start), group.kind,
            group.name, text[start:end]))
    if apply:
        out = text
        for start, end, m, group, _ in reversed(colors):
            out = out[:start] + convert(text, m, start) + out[end:]
        out = textures(out)
        if out != text:
            with open(path, "w") as f:
                f.write(out)
    return len(colors), len(unknown)


def main(argv):
    if len(argv) >= 2 and argv[1] in ("encode", "decode") and len(argv) == 5:
        fn = encode if argv[1] == "encode" else decode
        print(", ".join(str(fn(int(v))) for v in argv[2:5]))
        return 0
    if len(argv) >= 2 and argv[1] in ("scan", "apply"):
        ui = os.path.join(APP, "etc", "ui", "")
        files = argv[2:] or sorted(p for p in glob.glob(os.path.join(APP, "etc", "**", "*.flecs"),
            recursive=True) if not p.startswith(ui))
        total = unknown = 0
        for path in files:
            c, u = process(path, argv[1] == "apply")
            total += c
            unknown += u
        print("%d color literals, %d unknown integer literals" % (total, unknown))
        return 0
    print(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
