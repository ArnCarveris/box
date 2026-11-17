#!/usr/bin/env python3
import os
import subprocess
import sys

MAX_LINE = 100
MAX_FILE_LINES = 2000
MAX_TEMPLATE_LINES = 200
MAX_PARAGRAPH_LINES = 3
MAX_PARAGRAPHS = 2
VALUE_PREFIX = set(":=,([?")
FOLLOWS_BLOCK = ("}", ")", "]", "else")


class Line:
    def __init__(self, raw, code, comment, is_comment, in_string):
        self.raw = raw
        self.code = code
        self.comment = comment
        self.is_comment = is_comment
        self.in_string = in_string


def scan(lines):
    state = None
    out = []
    for raw in lines:
        text = raw.rstrip("\n")
        starts_in = state
        code = []
        comment = []
        has_comment = state == "block"
        i = 0
        n = len(text)
        while i < n:
            c = text[i]
            if state == "backtick":
                if c == "\\":
                    i += 2
                    continue
                if c == "`":
                    state = None
                    code.append("`")
                i += 1
                continue
            if state == "block":
                if text.startswith("*/", i):
                    state = None
                    i += 2
                    continue
                comment.append(c)
                i += 1
                continue
            if text.startswith("//", i):
                comment.append(text[i + 2:])
                has_comment = True
                break
            if text.startswith("/*", i):
                state = "block"
                has_comment = True
                i += 2
                continue
            if c == "`":
                state = "backtick"
                code.append("`")
                i += 1
                continue
            if c in "\"'":
                j = i + 1
                while j < n and text[j] != c:
                    j += 2 if text[j] == "\\" else 1
                code.append(c + c)
                i = j + 1
                continue
            code.append(c)
            i += 1
        code = "".join(code)
        is_comment = has_comment and not code.strip() and starts_in != "backtick"
        out.append(Line(text, code, "".join(comment), is_comment, starts_in == "backtick"))
    return out


def is_value_brace(prefix, stack):
    if stack and not isinstance(stack[-1], tuple):
        return True
    words = prefix.replace("(", " ").split()
    if "match" in words:
        return True
    prefix = prefix.rstrip()

    return bool(prefix) and prefix[-1] in VALUE_PREFIX


class Lint:
    def __init__(self, path, lines):
        self.path = path
        self.lines = scan(lines)
        self.problems = []

    def report(self, line, message):
        self.problems.append((self.path, line, message))

    def run(self):
        self.check_lines()
        self.check_blocks()
        self.check_comments()
        self.problems.sort(key=lambda p: p[1])
        return self.problems

    def check_lines(self):
        for i, line in enumerate(self.lines, 1):
            if len(line.raw) > MAX_LINE:
                self.report(i, f"line longer than {MAX_LINE} characters")
            if line.raw != line.raw.rstrip() and not line.in_string:
                self.report(i, "trailing whitespace")
        if len(self.lines) > MAX_FILE_LINES:
            self.report(len(self.lines), f"file longer than {MAX_FILE_LINES} lines")

    def closed_scopes(self):
        stack = []
        for i, line in enumerate(self.lines):
            prefix = ""
            for c in line.code:
                if c in "([":
                    stack.append(c)
                elif c == "{":
                    kind = "value" if is_value_brace(prefix, stack) else "scope"
                    stack.append((kind, i, prefix.split()[:1]) if kind == "scope" else kind)
                    prefix = ""
                    continue
                elif c in ")]" and stack and stack[-1] in ("(", "["):
                    stack.pop()
                elif c == "}" and stack:
                    top = stack.pop()
                    if isinstance(top, tuple):
                        yield top[1], i, top[2]
                    prefix = ""
                    continue
                elif c == ";":
                    prefix = ""
                    continue
                prefix += c

    def block_ends(self):
        for start, end, head in self.closed_scopes():
            if self.lines[end].code.strip() != "}":
                continue
            nxt = self.lines[end + 1].raw.strip() if end + 1 < len(self.lines) else ""
            if nxt and not nxt.startswith(FOLLOWS_BLOCK):
                yield end

    def check_blocks(self):
        for start, end, head in self.closed_scopes():
            if head == ["template"] and end - start > MAX_TEMPLATE_LINES:
                self.report(start + 1, f"template longer than {MAX_TEMPLATE_LINES} lines")
        for end in self.block_ends():
            self.report(end + 2, "blank line required after block")

    def comment_sections(self):
        i = 0
        n = len(self.lines)
        while i < n:
            if not self.lines[i].is_comment:
                i += 1
                continue
            j = i
            while j < n and self.lines[j].is_comment:
                j += 1
            yield i, j
            i = j

    def check_comments(self):
        for first, last in self.comment_sections():
            paragraphs = []
            current = None
            for k in range(first, last):
                text = self.lines[k].comment.strip().lstrip("*").strip()
                if not text:
                    current = None
                    continue
                if current is None:
                    current = [k, 0]
                    paragraphs.append(current)
                current[1] += 1
            for start, count in paragraphs:
                if count > MAX_PARAGRAPH_LINES:
                    self.report(start + 1,
                        f"comment paragraph longer than {MAX_PARAGRAPH_LINES} lines")
            if len(paragraphs) > MAX_PARAGRAPHS:
                self.report(paragraphs[MAX_PARAGRAPHS][0] + 1,
                    f"comment has more than {MAX_PARAGRAPHS} paragraphs")


def fix_file(path):
    with open(path) as f:
        original = f.read()
    lines = original.splitlines(True)
    lint = Lint(path, lines)
    blank_after = set(lint.block_ends())
    out = []
    for i, (raw, line) in enumerate(zip(lines, lint.lines)):
        if not line.in_string:
            ending = "\n" if raw.endswith("\n") else ""
            raw = raw.rstrip() + ending
        out.append(raw)
        if i in blank_after:
            out.append("\n")
    text = "".join(out)
    if text != original:
        with open(path, "w") as f:
            f.write(text)


def directory_scripts(path):
    try:
        listed = subprocess.run(
            ["git", "ls-files", "--cached", "--others", "--exclude-standard", "--", "."],
            cwd=path, capture_output=True, text=True, check=True).stdout.splitlines()
        files = [os.path.join(path, f) for f in listed]
    except (OSError, subprocess.CalledProcessError):
        files = []
        for root, dirs, names in os.walk(path):
            dirs[:] = [d for d in dirs if not d.startswith(".")]
            files += [os.path.join(root, name) for name in names]

    return sorted(f for f in files if f.endswith(".flecs") and os.path.isfile(f))


def main():
    args = sys.argv[1:]
    fix = "--fix" in args
    paths = [a for a in args if a != "--fix"]
    if not paths or any(not os.path.exists(p) for p in paths):
        print("usage: lint_flecs.py [--fix] <file|directory>...", file=sys.stderr)
        return 2
    files = []
    for path in paths:
        files += directory_scripts(path) if os.path.isdir(path) else [path]
    failed = 0
    for path in files:
        if fix:
            fix_file(path)
        with open(path) as f:
            problems = Lint(path, f.readlines()).run()
        for file, line, message in problems:
            print(f"{os.path.relpath(file)}:{line}: {message}")
        failed |= bool(problems)

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
