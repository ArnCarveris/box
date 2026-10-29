#!/usr/bin/env python3
"""Drives a box app through its command port, with the Python standard library only.

  app = App(binary, ["etc/scenes/x.flecs"], log="out/x.log")
  app.start()                      # --headless --delta-time 1/60 --timeout <s> --port <port> <args>
  app.exec("mouse.move [166, 298]")
  app.wait_input()                 # input.wait until 0
  app.wait_for("get showcase Showcase.mut.clicks", "3")
  app.get("box.picking.PickStatus", "PickStatus")["handler"]
  width, height, rows = app.capture()
  app.quit()                       # exit 0 within 10 s, /tmp/box/<pid> removed

start waits until /terminal/new answers and fails with the tail of the log when the process
exits first (Refused when the load guard refused it). Every command that fails raises Failure
with the command, its output and the log tail. The process runs in its own process group, which
kill (and leaving a with block on an error) kills, so no app keeps running. The output of the
process goes to the log file, which is kept.
"""
import json
import os
import shutil
import signal
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

from integration import ANSI, ROOT, read_png

ERROR = "error: "
START_TIMEOUT = 60
DELTA_TIME = "0.016666667"
REQUEST_TIMEOUT = 30
QUIT_TIMEOUT = 10
INPUT_FRAMES = 600
WAIT_FRAMES = 60
LOG_TAIL = 15
TMP_ROOT = "/tmp/box"
REFUSED = "cpu oversubscribed"


class Failure(AssertionError):
    """A check of a case failed: the message, the command it ran last and its output."""

    def __init__(self, message, command=None, output=None, log=None):
        super().__init__(message)
        self.command = command
        self.output = output
        self.log = log


class Refused(Failure):
    """The load guard of the app refused to start it."""


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Parser:
    """Parses a value as get prints it: numbers, true and false, quoted strings, struct values
    {a: 1, b: "x"} as dicts, arrays [1, 2] as lists, anything else (entity paths) as a string."""

    def __init__(self, text):
        self.text = text
        self.pos = 0

    def fail(self, what):
        raise ValueError("%s at %d of %r" % (what, self.pos, self.text))

    def skip(self):
        while self.pos < len(self.text) and self.text[self.pos] in " \t\r\n":
            self.pos += 1

    def peek(self):
        self.skip()
        return self.text[self.pos] if self.pos < len(self.text) else ""

    def expect(self, ch):
        if self.peek() != ch:
            self.fail("expected %r" % ch)
        self.pos += 1

    def value(self):
        ch = self.peek()
        if ch == "{":
            return self.struct()
        if ch == "[":
            return self.array()
        if ch == '"':
            return self.string()
        return self.word()

    def struct(self):
        self.expect("{")
        result = {}
        while self.peek() != "}":
            start = self.pos
            while self.pos < len(self.text) and self.text[self.pos] not in ":,}":
                self.pos += 1
            key = self.text[start:self.pos].strip()
            self.expect(":")
            result[key] = self.value()
            if self.peek() == ",":
                self.pos += 1
        self.expect("}")
        return result

    def array(self):
        self.expect("[")
        result = []
        while self.peek() != "]":
            result.append(self.value())
            if self.peek() == ",":
                self.pos += 1
        self.expect("]")
        return result

    def string(self):
        end = self.pos + 1
        while end < len(self.text) and self.text[end] != '"':
            end += 2 if self.text[end] == "\\" else 1
        if end >= len(self.text):
            self.fail("unterminated string")
        raw = self.text[self.pos:end + 1]
        self.pos = end + 1
        return json.loads(raw)

    def word(self):
        start = self.pos
        while self.pos < len(self.text) and self.text[self.pos] not in ",}]":
            self.pos += 1
        word = self.text[start:self.pos].strip()
        if word in ("true", "false"):
            return word == "true"
        try:
            return int(word)
        except ValueError:
            pass
        try:
            return float(word)
        except ValueError:
            return word

    def parse(self):
        result = self.value()
        if self.peek():
            self.fail("trailing text")
        return result


def parse_value(text):
    """Parse the output of get into Python values, see Parser."""
    return Parser(text.strip()).parse()


def srgb(color):
    """The bytes box decodes to the linear light of color bytes read as linear (c / 255), as
    flecs-engine reads them: round(255 * srgb_encode(c / 255)) per channel, for a tuple of any
    length (an alpha has to be kept by the caller)."""
    def encode(c):
        v = c / 255.0
        v = v * 12.92 if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055
        return int(v * 255.0 + 0.5)
    return tuple(encode(c) for c in color)


def mean_luma(image, box):
    """Mean luminance in 0..255 of the pixels of image (width, height, rows) in box (x0, y0, x1,
    y1), x1 and y1 exclusive."""
    width, height, rows = image
    x0, y0, x1, y1 = max(0, box[0]), max(0, box[1]), min(width, box[2]), min(height, box[3])
    total = 0
    for row in rows[y0:y1]:
        r, g, b = row[x0 * 3:x1 * 3:3], row[x0 * 3 + 1:x1 * 3:3], row[x0 * 3 + 2:x1 * 3:3]
        total += 0.299 * sum(r) + 0.587 * sum(g) + 0.114 * sum(b)
    count = (x1 - x0) * (y1 - y0)
    return total / count if count > 0 else 0.0


class App:
    """A headless app process with a terminal session on its command port."""

    def __init__(self, binary, args=(), log=None, timeout=90, out=None, prefix="app"):
        self.binary = binary
        self.args = list(args)
        self.log = log
        self.timeout = timeout
        self.out = out
        self.prefix = prefix
        self.proc = None
        self.port = None
        self.session = None
        self.deadline = None
        self.captures = 0
        self.exit_code = None
        self.last_command = None
        self.last_output = None

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, kind, value, tb):
        self.kill()
        return False

    def log_tail(self, lines=LOG_TAIL):
        if not self.log or not os.path.isfile(self.log):
            return ""
        with open(self.log, errors="replace") as f:
            return "\n".join(ANSI.sub("", f.read()).rstrip().splitlines()[-lines:])

    def fail(self, message, command=None, output=None):
        raise Failure(message, command if command is not None else self.last_command,
                      output if output is not None else self.last_output, self.log_tail())

    def start(self):
        self.port = free_port()
        cmd = [self.binary, "--headless", "--delta-time", DELTA_TIME, "--timeout",
               str(self.timeout), "--port", str(self.port)] + self.args
        log = open(self.log, "ab") if self.log else subprocess.DEVNULL
        try:
            if self.log:
                log.write(("$ %s\n" % " ".join(cmd)).encode())
                log.flush()
            self.proc = subprocess.Popen(cmd, cwd=ROOT, stdin=subprocess.DEVNULL, stdout=log,
                                         stderr=subprocess.STDOUT, start_new_session=True)
        finally:
            if self.log:
                log.close()
        self.deadline = time.time() + self.timeout
        limit = time.time() + START_TIMEOUT
        try:
            while True:
                code = self.proc.poll()
                if code is not None:
                    self.exit_code = code
                    tail = self.log_tail()
                    kind = Refused if REFUSED in tail else Failure
                    raise kind("the app exited with %d before its port answered" % code,
                               None, None, tail)
                try:
                    status, body = self.request("terminal/new")
                    if status == 200 and body:
                        self.session = body
                        return self
                    self.fail("/terminal/new answered %d" % status, "terminal/new", body)
                except (urllib.error.URLError, ConnectionError, socket.timeout):
                    pass
                if time.time() > limit:
                    self.fail("the port did not answer within %d s" % START_TIMEOUT,
                              "terminal/new", "")
                time.sleep(0.05)
        except BaseException:
            self.kill()
            raise

    def url(self, path, params=None):
        query = "?" + urllib.parse.urlencode(params, quote_via=urllib.parse.quote) \
            if params else ""
        return "http://127.0.0.1:%d/%s%s" % (self.port, path, query)

    def request(self, path, params=None, binary=False):
        """GET a path of the command port: the status code and the body."""
        try:
            with urllib.request.urlopen(self.url(path, params), timeout=REQUEST_TIMEOUT) as r:
                status, body = r.status, r.read()
        except urllib.error.HTTPError as e:
            status, body = e.code, e.read()
        return status, body if binary else body.decode(errors="replace")

    def running(self):
        return self.proc is not None and self.proc.poll() is None

    def exec(self, cmd):
        """Run a command line in the session; raise Failure when it fails."""
        self.last_command = cmd
        self.last_output = None
        if time.time() > self.deadline:
            self.fail("the case ran out of its %d s" % self.timeout, cmd, "")
        if not self.running():
            self.fail("the app is not running (exit %s)" % self.proc.poll(), cmd, "")
        try:
            status, out = self.request("terminal/exec", {"session": self.session, "cmd": cmd})
        except (urllib.error.URLError, ConnectionError, socket.timeout) as e:
            self.fail("no answer: %s" % e, cmd, "")
        self.last_output = out
        if out.startswith(ERROR) or status != 200:
            self.fail("the command failed (%d)" % status, cmd, out)
        return out

    def get(self, entity, component=None):
        """Get a component or member and parse it, see parse_value."""
        line = "get %s" % entity + (" %s" % component if component else "")
        out = self.exec(line)
        try:
            return parse_value(out)
        except ValueError as e:
            self.fail("cannot parse the output: %s" % e, line, out)

    def frame(self):
        return int(self.exec("get box.app.AppStatus AppStatus.frame"))

    def wait_frames(self, frames):
        """Let a number of frames pass, to check that something does not happen."""
        end = self.frame() + frames
        while self.frame() < end:
            pass

    def wait_input(self, frames=INPUT_FRAMES):
        """Poll input.wait until the input queue is drained."""
        start = None
        while True:
            out = self.exec("input.wait")
            if out.strip() == "0":
                return
            frame = self.frame()
            start = frame if start is None else start
            if frame - start > frames:
                self.fail("the input queue is not drained after %d frames" % frames,
                          "input.wait", out)

    def wait_for(self, cmd, predicate, frames=WAIT_FRAMES, what=None):
        """Run cmd until predicate(output) holds, or until the output equals predicate when it is
        a string; fail after a budget of frames with the last output. Returns the output."""
        check = predicate if callable(predicate) else (lambda out: out == predicate)
        start = None
        while True:
            out = self.exec(cmd)
            try:
                ok = check(out)
            except (ValueError, TypeError, KeyError):
                ok = False
            if ok:
                return out
            frame = self.frame()
            start = frame if start is None else start
            if frame - start > frames:
                expected = what or ("%r" % predicate if not callable(predicate) else
                                    "the expected value")
                self.fail("expected %s within %d frames, got %r" % (expected, frames, out),
                          cmd, out)

    def expect(self, cmd, expected):
        """Run cmd once and fail unless its output equals expected."""
        out = self.exec(cmd)
        if out != expected:
            self.fail("expected %r, got %r" % (expected, out), cmd, out)
        return out

    def check(self, condition, message):
        """Fail with message and the last command when condition is false."""
        if not condition:
            self.fail(message)

    def capture(self, name=None, camera=None, frames=WAIT_FRAMES):
        """Capture the active camera (or camera), wait for the PNG and return its pixels as
        (width, height, rows of RGB bytes). With out, the PNG is kept there as
        <prefix>.<name>.png."""
        line = "capture" + (" %s" % camera if camera else "")
        out = self.exec(line)
        resource = out.split("\n")[0].split(" ")[0]
        start = None
        while True:
            status, body = self.request("resource/" + resource, {"session": self.session},
                                        binary=True)
            if status == 200:
                break
            frame = self.frame()
            start = frame if start is None else start
            if frame - start > frames:
                self.fail("%s did not arrive within %d frames" % (resource, frames), line, out)
        name = name or "capture_%d" % self.captures
        self.captures += 1
        if self.out:
            path = os.path.join(self.out, "%s.%s.png" % (self.prefix, name))
        else:
            fd, path = tempfile.mkstemp(suffix=".png")
            os.close(fd)
        with open(path, "wb") as f:
            f.write(body)
        try:
            return read_png(path)
        finally:
            if not self.out:
                os.unlink(path)

    def tmp_dir(self):
        return os.path.join(TMP_ROOT, str(self.proc.pid))

    def quit(self):
        """Send quit, wait for exit 0 within 10 s and check that the temporary folder is gone."""
        self.exec("quit")
        try:
            code = self.proc.wait(QUIT_TIMEOUT)
        except subprocess.TimeoutExpired:
            self.kill()
            self.fail("the app did not exit within %d s of quit" % QUIT_TIMEOUT, "quit", "")
        self.exit_code = code
        if code != 0:
            self.fail("the app exited with %d after quit" % code, "quit", "")
        if os.path.exists(self.tmp_dir()):
            self.fail("%s is still there after quit" % self.tmp_dir(), "quit", "")

    def kill(self):
        """Kill the process group of the app and remove the temporary folder a killed app
        leaves."""
        if self.proc is None:
            return
        alive = self.proc.poll() is None
        try:
            os.killpg(self.proc.pid, signal.SIGKILL)
        except (ProcessLookupError, PermissionError):
            pass
        try:
            self.proc.wait(QUIT_TIMEOUT)
        except subprocess.TimeoutExpired:
            pass
        if alive:
            shutil.rmtree(self.tmp_dir(), ignore_errors=True)
