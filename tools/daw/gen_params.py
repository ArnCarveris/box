#!/usr/bin/env python3
import glob
import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEADERS = os.path.join(ROOT, "include", "box", "audio*.h")
OUT = os.path.join(ROOT, "tools", "daw", "params.js")

NUM = r"-?\d+(?:\.\d+)?(?:\s*/\s*\d+)?"
STRUCT = re.compile(r"(/\*\*(?:(?!\*/).)*\*/)\s*ECS_STRUCT\((\w+), \{(.*?)\n\}\);", re.S)
MEMBER = re.compile(r"^\s+([\w()]+(?:\s*\*)?)\s+(\w+)(?:\[(\d+)\])?;\s*/\*\*<(.*?)\*/", re.S | re.M)
UNITS = [
    ("Hz", "Hz"), ("dB", "dB"), ("Seconds", "s"), ("Milliseconds", "ms"), ("Beats", "beats"),
    ("Semitones", "st"), ("Cents", "ct"), ("Octaves", "oct"), ("MIDI note", "note"),
]


# Script names of the components whose C names start with Audio
NAMES = {"AudioSong": "SongFile"}


def script_name(name):
    if name in NAMES:
        return NAMES[name]
    if name.startswith("Audio") and name[5:6].isupper():
        return name[5:]
    return name


def number(text):
    text = text.replace(" ", "")
    if "/" in text:
        a, b = text.split("/")
        return float(a) / float(b)
    return float(text)


def clean(text):
    return " ".join(text.replace("*", " ").split())


def unit_of(name, doc):
    for prefix, unit in UNITS:
        if doc.startswith(prefix):
            return unit
    if doc.startswith("Linear gain") or doc.startswith("Linear,"):
        return "gain"
    for word, unit in (("in Hz", "Hz"), ("in dB", "dB"), ("Hz of", "Hz"), ("Seconds", "s"),
                       ("Milliseconds", "ms"), ("Semitones", "st"), ("Cents", "ct")):
        if word in doc:
            return unit
    if name in ("cutoff", "frequency", "center", "low_cut", "high_cut", "low", "high"):
        return "Hz"
    return ""


def range_of(doc):
    rate = re.search(r"(" + NUM + r")\s+to\s+0\.4\d+\s+(?:times\s+)?(?:of\s+)?the sample rate", doc)
    if rate:
        return number(rate.group(1)), 20000.0
    macro = re.search(r"(" + NUM + r")\s+to\s+[A-Z_]+\s+\((" + NUM + r")\)", doc)
    if macro:
        return number(macro.group(1)), number(macro.group(2))
    for plain in re.finditer(r"(?<![\w.])(" + NUM + r")\s*(?:to|\.\.)\s*(" + NUM + r")(?![\w.])", doc):
        if doc[:plain.start()].endswith("from "):
            continue
        lo, hi = number(plain.group(1)), number(plain.group(2))
        if lo < hi:
            return lo, hi
    return None


def default_of(doc):
    match = re.search(r"default\s+(?:all\s+)?(" + NUM + r")", doc)
    if match:
        return number(match.group(1))
    match = re.search(r"(" + NUM + r")\s+(?:\([^)]*\)\s*)?(?:is the default|\(the default\)|\(default\))", doc)
    if match:
        return number(match.group(1))
    return None


def guess(name, unit, default):
    if unit == "Hz":
        return 20.0, 20000.0
    if unit == "s":
        return 0.0, max(2.0, (default or 0) * 4)
    if unit == "ms":
        return 0.0, max(50.0, (default or 0) * 4)
    if unit == "gain":
        return 0.0, 4.0
    if unit == "beats":
        return 0.0, 16.0
    if unit == "dB":
        return -60.0, 12.0
    if unit == "st":
        return -24.0, 24.0
    if unit == "note":
        return 0.0, 127.0
    if name in ("pan", "balance"):
        return -1.0, 1.0
    if default is not None and default > 1:
        return 0.0, default * 4
    if default is not None and default < 0:
        return default * 4, -default * 4
    return 0.0, 1.0


def parse():
    src = "".join(open(path).read() for path in sorted(glob.glob(HEADERS)))
    params = {}
    docs = {}
    for match in STRUCT.finditer(src):
        struct_doc, name, body = match.group(1), script_name(match.group(2)), match.group(3)
        summary = clean(struct_doc.strip("/*"))
        docs[name] = summary.split(". ")[0].rstrip(".") + "."
        members = {}
        for member in MEMBER.finditer(body):
            ctype, mname, count, doc = member.groups()
            doc = clean(doc)
            info = {"doc": doc}
            if ctype in ("float", "double"):
                unit = unit_of(mname, doc)
                default = default_of(doc)
                found = range_of(doc)
                if found:
                    lo, hi = found
                else:
                    lo, hi = guess(mname, unit, default)
                    info["soft"] = True
                if default is not None and not (lo <= default <= hi):
                    lo, hi = min(lo, default), max(hi, default)
                info.update({"min": lo, "max": hi, "unit": unit})
                if default is not None:
                    info["def"] = default
                if unit in ("Hz", "s", "ms") and lo >= 0 and hi / max(lo, hi / 2000) >= 50:
                    info["log"] = True
                    if lo <= 0:
                        info["min"] = hi / 2000
                        info["zero"] = True
            members[mname] = info
        params[name] = members
    return params, docs


def main():
    params, docs = parse()
    with open(OUT, "w") as f:
        f.write("window.BOX_AUDIO_PARAMS = ")
        json.dump(params, f, indent=1, sort_keys=True)
        f.write(";\nwindow.BOX_AUDIO_DOCS = ")
        json.dump(docs, f, indent=1, sort_keys=True)
        f.write(";\n")
    print(f"wrote {OUT}: {len(params)} components")


if __name__ == "__main__":
    main()
