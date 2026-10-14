#!/usr/bin/env python3
"""Checks C sources against design/coding_style.c.

Usage: lint.py [--fix] [paths...]
       lint.py <file> <log|error|autofix>   (bake3 "lint" command)
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_PATHS = ["include", "src", "apps", "test", "bench"]
LEGACY = ["test/transform"]
EXTENSIONS = (".c", ".h", ".m")
BAKE_ACTIONS = ("log", "error", "autofix")
MAX_LINE = 100
MAX_FUNCTION_LINES = 200
MAX_FILE_LINES = 2000
MAX_PARAGRAPH_LINES = 3
MAX_PARAGRAPHS = 2
TEST_REF = re.compile(r"^\s*// [a-z0-9_]+(/[a-z0-9_]+){0,2}$")
FUNC_START = re.compile(r"^(?:static\s+|inline\s+|extern\s+)*[A-Za-z_][A-Za-z0-9_ \*]*?\b([A-Za-z_][A-Za-z0-9_]*)\s*\($")
FUNC_ONE_LINE = re.compile(r"^(?:static\s+|inline\s+|extern\s+)*[A-Za-z_][A-Za-z0-9_ \*]*?\b([A-Za-z_][A-Za-z0-9_]*)\s*\((.*)\)\s*\{?\s*$")
CONTROL = {"if", "for", "while", "switch", "return", "sizeof", "else", "do"}


class Lint:
    def __init__(self, path, lines):
        self.path = path
        self.lines = lines
        self.problems = []
        self.is_bench = path.startswith("bench/") or "/bench/" in path
        self.is_test = self.is_bench or path.startswith("test/") or "/test/" in path
        self.is_header = path.endswith(".h")

    def report(self, line, message):
        self.problems.append((self.path, line, message))

    def run(self):
        self.check_file_header()
        self.check_lines()
        self.check_functions()
        self.check_types()
        self.check_doc_paragraphs()
        return self.problems

    def check_file_header(self):
        head = "".join(self.lines[:12])
        if "@file" not in head or "@brief" not in head:
            self.report(1, "missing file header with @file and @brief")
        for i, line in enumerate(self.lines[:12], 1):
            match = re.search(r"@file\s+(\S+)", line)
            if not match:
                continue
            name = match.group(1)
            if self.path != name and not self.path.endswith("/" + name):
                self.report(i, f"@file {name} does not match file name")
            break
        if len(self.lines) > MAX_FILE_LINES:
            self.report(len(self.lines), f"file longer than {MAX_FILE_LINES} lines")

    def check_lines(self):
        for i, line in enumerate(self.lines, 1):
            if len(line.rstrip("\n")) > MAX_LINE:
                self.report(i, f"line longer than {MAX_LINE} characters")
            if line.rstrip("\n") != line.rstrip("\n").rstrip():
                self.report(i, "trailing whitespace")

    def strip_strings(self, line):
        return re.sub(r'"(?:\\.|[^"\\])*"', '""', re.sub(r"'(?:\\.|[^'\\])'", "''", line))

    def function_bodies(self):
        i = 0
        n = len(self.lines)
        while i < n:
            line = self.lines[i].rstrip("\n")
            stripped = line.strip()
            if line.startswith((" ", "\t", "#", "/", "*", "}")) or not stripped:
                i += 1
                continue
            multi = FUNC_START.match(stripped)
            single = FUNC_ONE_LINE.match(stripped)
            if not multi and not single:
                i += 1
                continue
            name = (multi or single).group(1)
            if name in CONTROL or stripped.startswith(("typedef", "struct ", "enum ", "ECS_", "return")):
                i += 1
                continue
            j = i
            while j < n and "{" not in self.lines[j] and ";" not in self.lines[j]:
                j += 1
            if j >= n or ";" in self.lines[j]:
                if self.is_header and j < n and self.lines[j].strip().endswith(");"):
                    yield name, i, j, None
                i = j + 1
                continue
            brace_line = j
            depth = 0
            k = brace_line
            while k < n:
                code = self.strip_strings(self.lines[k].split("//")[0])
                depth += code.count("{") - code.count("}")
                if depth == 0:
                    break
                k += 1
            yield name, i, brace_line, k
            i = k + 1

    def check_signature(self, name, start, brace_line):
        sig = "".join(self.lines[start:brace_line + 1])
        first = self.lines[start].rstrip("\n")
        if self.lines[brace_line].strip() != "{":
            self.report(brace_line + 1, f"{name}: opening brace must be on its own line")
        params = sig[sig.index("(") + 1:sig.rindex(")")].strip()
        if params and params != "void":
            if "(" in first and first.rstrip().endswith("(") is False:
                self.report(start + 1, f"{name}: parameters must start on the next line")
            for index, p in enumerate(params.split(",")):
                if index and "\n" not in p:
                    self.report(start + 1, f"{name}: each parameter on its own line")
                    break

    def check_comment(self, name, start, brace_line):
        j = start - 1
        while j >= 0 and self.lines[j].strip() == "":
            j -= 1
        if j < 0 or not self.lines[j].strip().endswith("*/"):
            is_static = self.lines[start].startswith("static")
            if (self.is_bench or not self.is_test) and (is_static or self.is_header):
                self.report(start + 1, f"{name}: missing doc comment")
            return
        k = j
        while k >= 0 and "/**" not in self.lines[k]:
            k -= 1
        comment = "".join(self.lines[k:j + 1])
        sig = "".join(self.lines[start:brace_line + 1])
        params = sig[sig.index("(") + 1:sig.rindex(")")].strip()
        if params and params != "void":
            for p in params.split(","):
                pname = re.sub(r"\[.*\]", "", p.strip()).split()[-1].lstrip("*")
                if f"@param {pname}" not in comment:
                    self.report(start + 1, f"{name}: doc comment misses @param {pname}")
        ret = sig[:sig.index(name)].strip()
        if not re.search(r"\bvoid\s*\*?$", ret) or ret.endswith("*"):
            if "@return" not in comment:
                self.report(start + 1, f"{name}: doc comment misses @return")
        elif "@return" in comment:
            self.report(start + 1, f"{name}: void function has @return")

    def check_body(self, name, brace_line, end):
        body = self.lines[brace_line + 1:end]
        if end - brace_line > MAX_FUNCTION_LINES:
            self.report(brace_line + 1, f"{name}: longer than {MAX_FUNCTION_LINES} lines")
        depth = 0
        for idx, raw in enumerate(body):
            lineno = brace_line + 2 + idx
            line = raw.rstrip("\n")
            stripped = line.strip()
            code = self.strip_strings(line)
            if "//" in code or "/*" in code:
                allowed = self.is_test and idx == 0 and TEST_REF.match(line)
                if not allowed:
                    self.report(lineno, f"{name}: comment inside function body")
            if re.match(r"return\b", stripped):
                if depth > 0:
                    self.report(lineno, f"{name}: return inside a block, use a single exit point")
                prev = body[idx - 1].strip() if idx > 0 else "{"
                if prev and not prev.endswith("{") and not prev.endswith(":") and not prev.startswith("#"):
                    self.report(lineno, f"{name}: blank line required before return")
            if stripped == "}" or stripped.startswith("} "):
                if stripped == "}":
                    nxt = body[idx + 1].strip() if idx + 1 < len(body) else ""
                    if nxt and not nxt.startswith(("}", "else", "#", "case", "default", "while")) \
                            and not nxt.endswith(":") and not nxt.startswith("*/"):
                        self.report(lineno + 1, f"{name}: blank line required after block")
            depth += code.split("//")[0].count("{") - code.split("//")[0].count("}")

    def check_functions(self):
        for name, start, brace_line, end in self.function_bodies():
            if end is None:
                self.check_comment(name, start, brace_line)
                continue
            if self.is_bench and self.lines[start].startswith("static"):
                self.check_signature(name, start, brace_line)
                self.check_comment(name, start, brace_line)
                self.check_body(name, brace_line, end)
                continue
            if self.is_test:
                self.check_body(name, brace_line, end)
                continue
            self.check_signature(name, start, brace_line)
            self.check_comment(name, start, brace_line)
            self.check_body(name, brace_line, end)

    def doc_blocks(self):
        i = 0
        n = len(self.lines)
        while i < n:
            start = self.lines[i].find("/**")
            if start == -1:
                i += 1
                continue
            j = i
            while j < n and "*/" not in self.lines[j][start + 3 if j == i else 0:]:
                j += 1
            yield i, min(j, n - 1)
            i = j + 1

    def doc_text(self, line):
        text = line.strip()
        if text.startswith("/**"):
            text = text[3:].lstrip("<")
        elif text.startswith("*") and not text.startswith("*/"):
            text = text[1:]
        if "*/" in text:
            text = text[:text.index("*/")]
        return text.strip()

    def check_doc_paragraphs(self):
        for first, last in self.doc_blocks():
            paragraphs = []
            current = None
            in_tag = False
            summary_line = None
            for k in range(first, last + 1):
                text = self.doc_text(self.lines[k])
                if not text:
                    current = None
                    in_tag = False
                    continue
                if text.startswith("@"):
                    current = None
                    in_tag = True
                    if summary_line is None:
                        summary_line = -1
                    continue
                if summary_line is None:
                    summary_line = k
                if in_tag:
                    continue
                if current is None:
                    current = [k, 0]
                    paragraphs.append(current)
                current[1] += 1
            for start, count in paragraphs:
                if count > MAX_PARAGRAPH_LINES:
                    self.report(start + 1,
                        f"doc paragraph longer than {MAX_PARAGRAPH_LINES} lines")
            if paragraphs and paragraphs[0][0] == summary_line:
                paragraphs = paragraphs[1:]
            if len(paragraphs) > MAX_PARAGRAPHS:
                self.report(paragraphs[MAX_PARAGRAPHS][0] + 1,
                    f"doc comment has more than {MAX_PARAGRAPHS} paragraphs")

    def check_types(self):
        for i, line in enumerate(self.lines):
            stripped = line.strip()
            if not (stripped.startswith("typedef struct") or stripped.startswith("ECS_STRUCT(")
                    or stripped.startswith("ECS_ENUM(") or stripped.startswith("typedef enum")):
                continue
            if not stripped.endswith("{"):
                continue
            j = i - 1
            while j >= 0 and self.lines[j].strip() == "":
                j -= 1
            if j < 0 or not self.lines[j].strip().endswith("*/"):
                self.report(i + 1, "type without doc comment")
            k = i + 1
            while k < len(self.lines) and not self.lines[k].strip().startswith("}"):
                member = self.lines[k].strip()
                if member and not member.startswith(("#", "/", "*")) and "/**<" not in member:
                    self.report(k + 1, "member without doc comment")
                k += 1


def collect(paths):
    for path in paths:
        full = os.path.join(ROOT, path)
        if os.path.isfile(full):
            yield full
            continue
        for dirpath, dirs, files in os.walk(full):
            if os.path.relpath(dirpath, ROOT) in LEGACY:
                dirs[:] = []
                continue
            dirs[:] = [d for d in dirs if not d.startswith(".") and d != "deliverable"]
            for name in sorted(files):
                inside = dirpath + "/"
                generated = "apps" not in dirpath or "/test/" in inside or "/bench/" in inside
                if name == "bake_config.h" or (name == "main.c" and generated):
                    continue
                if name.endswith(EXTENSIONS):
                    yield os.path.join(dirpath, name)


def fix_signature(lines, i):
    stripped = lines[i].strip()
    match = FUNC_ONE_LINE.match(stripped)
    if not match or match.group(1) in CONTROL or stripped.startswith(("typedef", "ECS_", "return")):
        return i + 1
    if lines[i].startswith((" ", "\t")):
        return i + 1
    params = match.group(2).strip()
    has_brace = stripped.endswith("{")
    if not has_brace and (i + 1 >= len(lines) or lines[i + 1].strip() != "{"):
        return i + 1
    head = stripped[:stripped.index("(")]
    if not params or params == "void":
        sig = f"{head}(void)"
    else:
        sig = head + "(\n" + ",\n".join("    " + p.strip() for p in params.split(",")) + ")"
    replacement = sig + "\n{\n"
    end = i + 1 if has_brace else i + 2
    lines[i:end] = [replacement]
    return i + 1


def fix_blank_lines(lines, lint):
    wanted = set()
    for name, start, brace_line, end in lint.function_bodies():
        if end is None:
            continue
        body = lines[brace_line + 1:end]
        for idx, raw in enumerate(body):
            stripped = raw.strip()
            at = brace_line + 1 + idx
            if re.match(r"return\b", stripped):
                prev = body[idx - 1].strip() if idx > 0 else "{"
                if prev and not prev.endswith("{") and not prev.endswith(":") \
                        and not prev.startswith("#"):
                    wanted.add(at)
            if stripped == "}":
                nxt = body[idx + 1].strip() if idx + 1 < len(body) else ""
                if nxt and not nxt.startswith(("}", "else", "#", "case", "default", "while")) \
                        and not nxt.endswith(":") and not nxt.startswith("*/"):
                    wanted.add(at + 1)
    out = []
    for idx, line in enumerate(lines):
        if idx in wanted:
            out.append("\n")
        out.append(line)
    return out


def fix_file(path, rel=None):
    with open(path) as f:
        original = f.read()
    lines = original.splitlines(True)
    rel = rel or os.path.relpath(path, ROOT)
    lint = Lint(rel, lines)
    i = 0
    while i < len(lines):
        if lint.is_test and not (lint.is_bench and lines[i].startswith("static")):
            i += 1
            continue
        i = fix_signature(lines, i)
    lines = "".join(lines).splitlines(True)
    lint = Lint(rel, lines)
    lines = fix_blank_lines(lines, lint)
    text = "".join(lines)
    if text != original:
        with open(path, "w") as f:
            f.write(text)


def lint_one(path, action):
    if action == "autofix":
        fix_file(path)
    with open(path) as f:
        lines = f.readlines()
    problems = Lint(os.path.relpath(path, ROOT), lines).run()
    for rel, line, message in problems:
        print(f"{rel}:{line}: {message}")

    return 1 if problems else 0


def main():
    if len(sys.argv) == 3 and sys.argv[2] in BAKE_ACTIONS and os.path.isfile(sys.argv[1]):
        return lint_one(os.path.abspath(sys.argv[1]), sys.argv[2])
    fix = "--fix" in sys.argv
    paths = [a for a in sys.argv[1:] if a != "--fix"] or DEFAULT_PATHS
    if fix:
        for path in collect(paths):
            fix_file(path)
    problems = []
    for path in collect(paths):
        with open(path) as f:
            lines = f.readlines()
        problems.extend(Lint(os.path.relpath(path, ROOT), lines).run())
    for path, line, message in problems:
        print(f"{path}:{line}: {message}")
    print(f"{len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
