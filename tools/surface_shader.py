#!/usr/bin/env python3
"""Generates etc/shaders/include/surface.wgsl from the surface code in src/geometry/surface_math.c.

Usage: surface_shader.py [output]   (default: etc/shaders/include/surface.wgsl)

Translates the static functions from sv() up to box_surface_sample() so that the CPU and GPU
evaluate parametric surfaces with the same math. The C code keeps a single exit point, the shader
returns early: an assignment to the returned variable that is followed only by the final return
becomes a return, and so does one that is followed by setting a "found" flag. The surface data is
the global surface_data of the shader instead of a parameter. tools/test_surface_shader.py checks
that the committed shader is what this script writes.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, "src", "geometry", "surface_math.c")
OUTPUT = os.path.join(ROOT, "etc", "shaders", "include", "surface.wgsl")
FIRST = "static box_sv sv("
LAST = "void box_surface_sample("
DATA = "const box_surface_data_t *d"
TYPES = {"box_sv": "vec3f", "float": "f32", "int": "i32", "bool": "bool",
         "box_sweep_sample": "SurfaceSweepSample"}
MATH = {"sqrtf": "sqrt", "fminf": "min", "fmaxf": "max", "fabsf": "abs", "floorf": "floor",
        "cosf": "cos", "sinf": "sin"}
HEADER = ('#pragma once\n\n#include "curve.wgsl"\n\nstruct SurfaceData {\n'
          '    p: array<vec4f, 24>,\n};\n\nvar<private> surface_data: SurfaceData;\n')
TYPE_NAMES = "|".join(TYPES)


# Statements

class Stmt:
    """A simple statement; lines after the first keep their indentation relative to the first."""

    def __init__(self, lines):
        self.lines = lines

    def text(self):
        return "\n".join(self.lines)


class Blank:
    pass


class If:
    """An if chain: branches of (condition, body) and an optional else body."""

    def __init__(self, branches, otherwise):
        self.branches = branches
        self.otherwise = otherwise


class For:
    def __init__(self, header, body):
        self.header = header
        self.body = body


def indent_of(line):
    return len(line) - len(line.lstrip(" "))


def closing(lines, start, indent):
    """Index of the line closing the block opened at lines[start]."""
    for i in range(start + 1, len(lines)):
        if lines[i].strip() and indent_of(lines[i]) == indent and lines[i].lstrip().startswith("}"):
            return i
    raise ValueError("unclosed block: " + lines[start])


def parse(lines):
    """Parse the lines of a block, all at the indentation of the first line or deeper."""
    nodes = []
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()
        if not stripped:
            nodes.append(Blank())
            i += 1
            continue
        indent = indent_of(line)
        if stripped.startswith("if (") and stripped.endswith("{"):
            branches = []
            otherwise = None
            condition = stripped[len("if "):-len(" {")]
            while True:
                end = closing(lines, i, indent)
                body = parse(lines[i + 1:end])
                tail = lines[end].strip()
                if condition is None:
                    otherwise = body
                else:
                    branches.append((condition, body))
                i = end
                if tail.startswith("} else if (") and tail.endswith("{"):
                    condition = tail[len("} else if "):-len(" {")]
                elif tail == "} else {":
                    condition = None
                elif tail == "}":
                    break
                else:
                    raise ValueError("unexpected line: " + tail)
            nodes.append(If(branches, otherwise))
            i += 1
        elif stripped.startswith("for (") and stripped.endswith("{"):
            end = closing(lines, i, indent)
            nodes.append(For(stripped[:-len(" {")], parse(lines[i + 1:end])))
            i = end + 1
        else:
            statement = [stripped]
            while not lines[i].rstrip().endswith(";"):
                i += 1
                statement.append(" " * (indent_of(lines[i]) - indent) + lines[i].strip())
            nodes.append(Stmt(statement))
            i += 1
    return nodes


def emit(nodes, indent):
    pad = " " * indent
    out = []
    for node in nodes:
        if isinstance(node, Blank):
            out.append("")
        elif isinstance(node, Stmt):
            out.extend(pad + line for line in node.lines)
        elif isinstance(node, For):
            out.append(pad + node.header + " {")
            out.extend(emit(node.body, indent + 4))
            out.append(pad + "}")
        else:
            for k, (condition, body) in enumerate(node.branches):
                out.append(pad + ("if " if k == 0 else "} else if ") + condition + " {")
                out.extend(emit(body, indent + 4))
            if node.otherwise is not None:
                out.append(pad + "} else {")
                out.extend(emit(node.otherwise, indent + 4))
            out.append(pad + "}")
    return out


def tidy(nodes):
    """Drop blank lines at the start and end of a block and runs of blank lines."""
    out = []
    for node in nodes:
        if isinstance(node, Blank) and (not out or isinstance(out[-1], Blank)):
            continue
        out.append(node)
    while out and isinstance(out[-1], Blank):
        out.pop()
    return out


def last_index(nodes):
    indices = [i for i, node in enumerate(nodes) if not isinstance(node, Blank)]
    return indices[-1] if indices else -1


def returns(nodes):
    i = last_index(nodes)
    return i >= 0 and isinstance(nodes[i], Stmt) and nodes[i].lines[0].startswith("return ")


def mentions(nodes, name):
    pattern = re.compile(r"\b" + name + r"\b")
    count = 0
    for node in nodes:
        if isinstance(node, Stmt):
            count += len(pattern.findall(node.text()))
        elif isinstance(node, For):
            count += len(pattern.findall(node.header)) + mentions(node.body, name)
        elif isinstance(node, If):
            for condition, body in node.branches:
                count += len(pattern.findall(condition)) + mentions(body, name)
            if node.otherwise is not None:
                count += mentions(node.otherwise, name)
    return count


# Early returns

class Exits:
    """Rewrites the single exit of a function into early returns.

    result is the variable the function returns at its end, flags are the booleans that skip the
    rest of the function once set.
    """

    def __init__(self, result, flags):
        self.result = result
        self.flags = flags

    def is_flag_set(self, node):
        return isinstance(node, Stmt) and any(node.lines == [flag + " = true;"]
                                              for flag in self.flags)

    def is_flag_test(self, node):
        return (isinstance(node, If) and node.otherwise is None and len(node.branches) == 1 and
                node.branches[0][0] in ["(!" + flag + ")" for flag in self.flags])

    def is_flag_declaration(self, node):
        return isinstance(node, Stmt) and any(node.lines == ["bool " + flag + " = false;"]
                                              for flag in self.flags)

    def block(self, nodes, tail):
        """Rewrite a block; tail is set when the function returns result right after it."""
        for k, node in enumerate(nodes):
            if self.is_flag_set(node):
                if last_index(nodes[k + 1:]) >= 0:
                    raise ValueError("statements after setting a flag")
                return self.block(nodes[:k], True)
        out = []
        last = last_index(nodes)
        for i, node in enumerate(nodes):
            at_tail = tail and i == last
            if isinstance(node, Stmt):
                if self.is_flag_declaration(node):
                    continue
                if at_tail and self.result and node.lines[0].startswith(self.result + " = "):
                    node = Stmt(["return " + node.lines[0][len(self.result + " = "):]] +
                                node.lines[1:])
                out.append(node)
            elif isinstance(node, For):
                header = node.header
                for flag in self.flags:
                    header = header.replace("!" + flag + " && ", "")
                out.append(For(header, tidy(self.block(node.body, False))))
            elif self.is_flag_test(node):
                # Every flag set became a return, so the flag is always clear here.
                out.extend(self.block(node.branches[0][1], at_tail))
            elif isinstance(node, If):
                branches = [(c, tidy(self.block(b, at_tail))) for c, b in node.branches]
                otherwise = None
                if node.otherwise is not None:
                    otherwise = tidy(self.block(node.otherwise, at_tail))
                out.extend(split(branches, otherwise))
            else:
                out.append(node)
        return out

    def function(self, nodes):
        nodes = tidy(nodes)
        last = last_index(nodes)
        body = self.block(nodes[:last], True)
        if not returns(body):
            body = body + nodes[last:]
        body = tidy(body)
        for i, node in enumerate(body):
            match = isinstance(node, Stmt) and re.match(
                r"(?:" + TYPE_NAMES + r") " + self.result + r"(?: = (.*))?;$", node.lines[0])
            if not match:
                continue
            rest = body[:i] + body[i + 1:]
            uses = mentions(rest, self.result)
            final = rest[-1].lines if isinstance(rest[-1], Stmt) else None
            if uses == 0:
                body = rest
            elif uses == 1 and match[1] and final == ["return " + self.result + ";"]:
                rest[-1] = Stmt(["return " + match[1] + ";"])
                body = rest
            break
        return tidy(body)


def split(branches, otherwise):
    """Split an if chain after each branch that returns, as no else is needed after a return."""
    out = []
    pending = []
    for condition, body in branches:
        pending.append((condition, body))
        if len(pending) == 1 and returns(body):
            out.extend([If(pending, None), Blank()])
            pending = []
    if pending:
        out.append(If(pending, otherwise))
    elif otherwise is not None:
        out.extend(otherwise)
    return out


# Expressions and declarations

def operand_end(text, start):
    """End of the cast operand starting at text[start]: a call, a parenthesis, or a name."""
    i = start
    if text[i] != "(":
        while i < len(text) and (text[i].isalnum() or text[i] in "_."):
            i += 1
        if i == len(text) or text[i] != "(":
            return i
    depth = 0
    while True:
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1


def casts(text):
    """Turn (float)x and (int)x into f32(x) and i32(x)."""
    match = re.search(r"\((float|int)\)", text)
    while match:
        start = match.end()
        end = operand_end(text, start)
        operand = text[start:end]
        if operand.startswith("(") and operand.endswith(")") and operand_end(operand, 0) == len(operand):
            operand = operand[1:-1]
        text = text[:match.start()] + TYPES[match[1]] + "(" + operand + ")" + text[end:]
        match = re.search(r"\((float|int)\)", text)
    return text


def expression(text):
    text = text.replace("d->assets[", "surface_assets[").replace("d->p[", "surface_data.p[")
    text = re.sub(r"\(d, ", "(", text).replace("(d)", "()")
    text = re.sub(r"\(box_sv\)\{([^{}]*)\}", r"vec3f(\1)", text)
    text = re.sub(r"\b(?:" + TYPE_NAMES + r") (\w+) =", r"var \1 =", text)
    text = re.sub(r"^(\s*)(" + TYPE_NAMES + r") (\w+);$",
                  lambda m: m[1] + "var " + m[3] + ": " + TYPES[m[2]] + ";", text, flags=re.M)
    text = casts(text)
    text = re.sub(r"(?<![\w.])(\d+\.\d*|\d+)f\b", r"\1", text)
    return re.sub(r"\b(" + "|".join(MATH) + r")\b", lambda m: MATH[m[1]], text)


# Top level

def chunks(lines):
    """Split the source into doc comments, typedefs, and functions."""
    i = 0
    while i < len(lines):
        line = lines[i]
        if not line.strip():
            i += 1
        elif line.startswith("/**"):
            while "*/" not in lines[i]:
                i += 1
            i += 1
        elif line.startswith("typedef struct"):
            start = i
            while not lines[i].startswith("}"):
                i += 1
            yield "struct", lines[start:i + 1]
            i += 1
        else:
            start = i
            while lines[i] != "}":
                i += 1
            yield "function", lines[start:i + 1]
            i += 1


def struct(lines):
    name = re.match(r"\} (\w+);", lines[-1])[1]
    out = ["struct " + TYPES[name] + " {"]
    for line in lines[1:-1]:
        match = re.match(r"\s*(\w+) (\w+);", line)
        out.append("    " + match[2] + ": " + TYPES[match[1]] + ",")
    return "\n".join(out + ["};"])


def function(lines):
    brace = lines.index("{")
    signature = " ".join(line.strip() for line in lines[:brace])
    match = re.match(r"static (\w+) (\w+)\((.*)\)$", signature)
    if not match:
        return None
    kind, name, params = match.groups()
    params = [p.strip() for p in params.split(",") if p.strip() != DATA]
    params = ", ".join(p.split()[1] + ": " + TYPES[p.split()[0]] for p in params)
    body = parse(lines[brace + 1:-1])
    last = body[last_index(body)]
    final = re.match(r"return (\w+);$", last.lines[0]) if isinstance(last, Stmt) else None
    result = final[1] if final else None
    flags = re.findall(r"^\s*bool (\w+) = false;$", "\n".join(lines), flags=re.M)
    if result and mentions(body, result) > 1:
        body = Exits(result, flags).function(body)
    text = expression("\n".join(emit(body, 4)))
    return "fn " + name + "(" + params + ") -> " + TYPES[kind] + " {\n" + text + "\n}"


def generate(source=SOURCE):
    """Return the WGSL translation of the surface code in source."""
    with open(source) as f:
        text = f.read()
    text = text[text.index(FIRST):text.index(LAST)]
    parts = [HEADER.rstrip("\n")]
    for kind, lines in chunks(text.split("\n")):
        part = struct(lines) if kind == "struct" else function(lines)
        if part:
            parts.append(part)
    return "\n\n".join(parts) + "\n"


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    output = argv[0] if argv else OUTPUT
    with open(output, "w") as f:
        f.write(generate())
    return 0


if __name__ == "__main__":
    sys.exit(main())
