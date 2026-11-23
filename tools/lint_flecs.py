#!/usr/bin/env python3
import os
import re
import subprocess
import sys

MAX_LINE = 100
MAX_FILE_LINES = 2000
MAX_TEMPLATE_LINES = 200
MAX_PARAGRAPH_LINES = 3
MAX_PARAGRAPHS = 2
INDENT = 2
VALUE_PREFIX = set(":=,([?")
FOLLOWS_BLOCK = ("}", ")", "]", "else")
STATEMENT = re.compile(r"(if|for|while)\b")
CONTINUES = re.compile(r"(&&|\|\||[-+*/%?:=<>|&!^])$")
CONTINUED = re.compile(r"^(&&|\|\||[.?:+*/%<>|&^=])")
VALUE_HEAD = re.compile(r"^((\([^()]*\)|[\w.$]+)\s*:\s*)?$")
KEYWORDS = {"if", "for", "while", "else", "with", "match", "const", "prop", "mut", "using",
    "module", "export", "fn", "async", "include"}
CALL_HEAD = re.compile(r"^((emit|struct)\s+)?[\w.$]+(\s+(\"[^\"]*\"|[\w.$]+))?\s*$")


class Line:
    def __init__(self, raw, code, comment, is_comment, starts_in):
        self.raw = raw
        self.code = code
        self.comment = comment
        self.is_comment = is_comment
        self.has_comment = bool(comment) or "//" in raw or "/*" in raw
        self.in_string = starts_in == "backtick"
        self.in_block = starts_in == "block"


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
        out.append(Line(text, code, "".join(comment), is_comment, starts_in))
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
        self.check_indentation()
        self.check_blocks()
        self.check_comments()
        self.problems.sort(key=lambda p: p[1])
        return self.problems

    def check_lines(self):
        for i, line in enumerate(self.lines, 1):
            if len(line.raw) > MAX_LINE and not line.in_string:
                self.report(i, f"line longer than {MAX_LINE} characters")
            if line.raw != line.raw.rstrip() and not line.in_string:
                self.report(i, "trailing whitespace")
        if len(self.lines) > MAX_FILE_LINES:
            self.report(len(self.lines), f"file longer than {MAX_FILE_LINES} lines")

    def indents(self):
        stack = []
        previous = ""
        for i, line in enumerate(self.lines):
            base = stack[-1][1] if stack else 0
            code = line.code.strip()
            if code[:1] in ("}", ")", "]") and stack:
                base = stack[-1][0]
            continuation = CONTINUES.search(previous) or CONTINUED.match(code)
            allowed = (base, base + INDENT) if continuation else (base,)
            if not (line.in_string or line.in_block or not line.raw.strip()):
                indent = line.raw[:len(line.raw) - len(line.raw.lstrip())]
                if indent not in [" " * n for n in allowed]:
                    yield i, allowed[-1]
            for c in line.code:
                if c in "{([":
                    stack.append((base, base + INDENT))
                elif c in "})]" and stack:
                    base = min(base, stack.pop()[0])
            if code and not line.in_string:
                previous = code

    def check_indentation(self):
        for i, expected in self.indents():
            self.report(i + 1, f"indentation should be {expected} spaces")

    def closed_scopes(self):
        for event in self.walk():
            if event[0] == "close":
                yield event[1:]

    def value_lines(self):
        return {e[1] for e in self.walk() if e[0] == "line" and e[2] and not isinstance(e[2], tuple)}

    def walk(self):
        stack = []
        for i, line in enumerate(self.lines):
            yield "line", i, stack[-1] if stack else None
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
                        yield "close", top[1], i, top[2]
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

    def statement_starts(self):
        for i, line in enumerate(self.lines):
            if line.in_string or not STATEMENT.match(line.code.strip()):
                continue
            j = i - 1
            while j >= 0 and self.lines[j].is_comment:
                j -= 1
            if j < 0 or not self.lines[j].raw.strip():
                continue
            if self.lines[j].code.rstrip().endswith("{"):
                continue
            yield i, j + 1

    def blanks_before_close(self):
        for i, line in enumerate(self.lines):
            if line.in_string or not line.code.strip().startswith("}"):
                continue
            j = i - 1
            while j >= 0 and not self.lines[j].raw.strip() and not self.lines[j].in_string:
                yield j
                j -= 1

    def check_blocks(self):
        for start, end, head in self.closed_scopes():
            if head == ["template"] and end - start > MAX_TEMPLATE_LINES:
                self.report(start + 1, f"template longer than {MAX_TEMPLATE_LINES} lines")
        for end in self.block_ends():
            self.report(end + 2, "blank line required after block")
        for i, gap in self.statement_starts():
            keyword = STATEMENT.match(self.lines[i].code.strip()).group(1)
            self.report(i + 1, f"blank line required before {keyword}")
        for i in self.blanks_before_close():
            self.report(i + 1, "blank line before closing brace")

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


def brackets(text):
    depth = 0
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c in "\"'`":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            i = j + 1
            continue
        if c in "})]":
            depth -= 1
        yield i, c, depth
        if c in "{([":
            depth += 1
        i += 1


def split_top(text, sep):
    parts = []
    start = 0
    for i, c, depth in brackets(text):
        if c == sep and depth == 0:
            parts.append(text[start:i])
            start = i + 1
    parts.append(text[start:])

    return [p.strip() for p in parts if p.strip()]


def entity_parts(stmt, allow_open):
    word = re.match(r"[A-Za-z_]\w*", stmt)
    if stmt.startswith(("@", "}")) or (word and word.group(0) in KEYWORDS):
        return None
    opened = None
    for i, c, depth in brackets(stmt):
        if depth < 0:
            return None
        if c == "{" and depth == 0 and opened is None:
            if is_value_brace(stmt[:i], []):
                return None
            opened = i
        elif c == "}" and depth == 0 and opened is not None:
            if i != len(stmt) - 1:
                return None

            return stmt[:opened].rstrip(), split_top(stmt[opened + 1:i], ";"), True
    if opened is not None and allow_open:
        return stmt[:opened].rstrip(), split_top(stmt[opened + 1:], ";"), False

    return None


def if_chain(stmt):
    chain = []
    rest = stmt
    while rest:
        opened = None
        end = None
        for i, c, depth in brackets(rest):
            if depth < 0:
                return None
            if c == "{" and depth == 0 and opened is None and not is_value_brace(rest[:i], []):
                opened = i
            elif c == "}" and depth == 0 and opened is not None:
                end = i
                break
        if end is None:
            return None
        head = rest[:opened].strip()
        expected = r"if\b.+" if not chain else r"else(\s+if\b.+)?"
        if not re.fullmatch(expected, head) or (chain and chain[-1][0] == "else"):
            return None
        chain.append((head, split_top(rest[opened + 1:end], ";")))
        rest = rest[end + 1:].strip()

    return chain or None


def value_parts(member):
    if member[-1:] not in ("}", "]"):
        return None
    stack = []
    for i, c, depth in brackets(member):
        if depth < 0:
            return None
        if c in "{([":
            stack.append(i)
        elif c in "})]" and stack:
            opened = stack.pop()
            if i == len(member) - 1 and not stack:
                head = member[:opened]
                if not VALUE_HEAD.match(head.strip()):
                    return None

                return head, member[opened], split_top(member[opened + 1:i], ","), c

    return None


def format_member(member, indent, trailing):
    line = indent + member + trailing
    parts = value_parts(member) if len(line) > MAX_LINE else None
    if not parts or not parts[2]:
        return [line]
    head, opened, items, closed = parts
    inner = indent + " " * INDENT
    out = [indent + head + opened]
    for k, item in enumerate(items):
        out += format_member(item, inner, "," if k < len(items) - 1 else "")
    out.append(indent + closed + trailing)

    return out


def format_members(text, indent):
    trailing = "," if text.endswith(",") else ""
    members = split_top(text[:len(text) - len(trailing)], ",")
    out = []
    for k, member in enumerate(members):
        out += format_member(member, indent, "," if k < len(members) - 1 else trailing)

    return out


def call_parts(text):
    if not text.endswith(")"):
        return None
    stack = []
    for i, c, depth in brackets(text):
        if depth < 0:
            return None
        if c in "{([":
            stack.append(i)
        elif c in "})]" and stack:
            opened = stack.pop()
            if i == len(text) - 1 and not stack:
                head = text[:opened]
                args = split_top(text[opened + 1:i], ",")
                if c != ")" or not args or not CALL_HEAD.match(head):
                    return None

                return head.rstrip(), args

    return None


def format_call(text, indent, suffix):
    line = indent + text + suffix
    parts = call_parts(text) if len(line) > MAX_LINE else None
    if not parts:
        return [line]
    head, args = parts
    inner = indent + " " * INDENT
    if len(args) == 1:
        single = format_member(args[0], inner, "")
        if len(single) == 1 and len(single[0]) > MAX_LINE:
            return [line]
    out = [indent + head + "("]
    for k, arg in enumerate(args):
        out += format_member(arg, inner, "," if k < len(args) - 1 else "")
    out.append(indent + ")" + suffix)

    return out


def match_parts(stmt):
    start = None
    opened = None
    for i, c, depth in brackets(stmt):
        if depth < 0:
            return None
        if start is None:
            if depth == 0 and re.match(r"match\b", stmt[i:]) and \
                    (i == 0 or not re.match(r"[\w.$]", stmt[i - 1])):
                start = i
        elif c == "{" and depth == 0 and opened is None:
            opened = i
        elif c == "}" and depth == 0 and opened is not None:
            if i != len(stmt) - 1:
                return None

            return stmt[:opened].rstrip(), split_top(stmt[opened + 1:i], ";")

    return None


def format_statement(stmt, indent, allow_open=False):
    if len(indent + stmt) <= MAX_LINE:
        return [indent + stmt]
    chain = if_chain(stmt)
    if chain:
        out = []
        for k, (head, body) in enumerate(chain):
            out.append(indent + ("} " if k else "") + head + " {")
            for item in body:
                out += format_statement(item, indent + " " * INDENT)
        out.append(indent + "}")

        return out
    match = match_parts(stmt)
    if match and len(match[1]) > 1:
        head, arms = match
        out = [indent + head + " {"]
        for arm in arms:
            out += format_member(arm, indent + " " * INDENT, "")
        out.append(indent + "}")

        return out
    word = re.match(r"[A-Za-z_]\w*", stmt)
    if word and word.group(0) in KEYWORDS:
        return [indent + stmt]
    parts = entity_parts(stmt, allow_open)
    if not parts:
        if call_parts(stmt):
            return format_call(stmt, indent, "")

        return format_member(stmt, indent, "")
    head, body, closed = parts
    if not body:
        return format_call(head, indent, " {}" if closed else " {")
    out = format_call(head, indent, " {") if head else [indent + "{"]
    for item in body:
        out += format_statement(item, indent + " " * INDENT)
    if closed:
        out.append(indent + "}")

    return out


def balanced(text):
    marks = list(brackets(text))
    opened = sum(1 for i, c, depth in marks if c in "{([")
    closed = sum(1 for i, c, depth in marks if c in "})]")

    return opened == closed and all(depth >= 0 for i, c, depth in marks)


def statement_end(scanned, i):
    depth = 0
    text = ""
    for j in range(i, min(len(scanned), i + 64)):
        line = scanned[j]
        if line.in_string or line.in_block or line.has_comment or "`" in line.code \
                or re.search(r"\bmatch\b", line.code):
            return None
        code = line.code.strip()
        for k, c in enumerate(code):
            if c == "{" and not depth and not is_value_brace(text + code[:k], []):
                return j if j > i and not code[k + 1:].strip() else None
            if c in "{([":
                depth += 1
            elif c in "})]":
                depth -= 1
                if depth < 0:
                    return None
        if not depth:
            return j if j > i else None
        text += code + " "

    return None


def split_long_lines(lines):
    lint = Lint("", lines)
    in_value = lint.value_lines()
    out = []
    i = 0
    while i < len(lines):
        raw = lines[i]
        line = lint.lines[i]
        text = raw.rstrip()
        i += 1
        if len(text) <= MAX_LINE or line.in_string or line.in_block or line.has_comment \
                or "`" in line.code:
            out.append(raw)
            continue
        indent = text[:len(text) - len(text.lstrip())]
        end = None if i - 1 in in_value else statement_end(lint.lines, i - 1)
        if end is not None:
            joined = " ".join(l.strip() for l in lines[i - 1:end + 1])
            split = format_statement(joined, indent, True)
            if len(split) > 1:
                out += [l + "\n" for l in split]
                i = end + 1
                continue
        if i - 1 in in_value:
            split = format_members(text.strip(), indent) if balanced(text) else [text]
        else:
            split = format_statement(text.strip(), indent, True)
        out += [l + "\n" for l in split]

    return out


def fix_file(path):
    with open(path) as f:
        original = f.read()
    lines = split_long_lines(original.splitlines(True))
    lint = Lint(path, lines)
    blank_before = {end + 1 for end in lint.block_ends()}
    blank_before |= {gap for i, gap in lint.statement_starts()}
    indents = dict(lint.indents())
    removed = set(lint.blanks_before_close())
    out = []
    for i, (raw, line) in enumerate(zip(lines, lint.lines)):
        if i in removed:
            continue
        if i in blank_before:
            out.append("\n")
        if i in indents:
            raw = " " * indents[i] + raw.lstrip()
        if not line.in_string:
            ending = "\n" if raw.endswith("\n") else ""
            raw = raw.rstrip() + ending
        out.append(raw)
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
