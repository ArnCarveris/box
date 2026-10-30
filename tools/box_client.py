#!/usr/bin/env python3
"""Drives a box app through the flecs shell endpoints of its REST port, with the Python standard
library only.

  app = App(binary, ["etc/scenes/x.flecs"], log="out/x.log")
  app.start()                      # --headless --delta-time 1/60 --timeout <s> --port <port> <args>
  app.exec("mouse_move [166, 298]")
  app.wait_input()                 # input_wait until 0
  app.wait_for("get showcase Showcase.mut.clicks", "3")
  app.get("box.picking.PickStatus", "PickStatus")["handler"]
  app.edit("on")                   # edit on, True while the editor is open
  width, height, rows = app.capture()
  app.quit()                       # exit 0 within 10 s, /tmp/box/<pid> removed

start opens a shell session (PUT /shell/session) once the port answers and fails with the tail of
the log when the process exits first (Refused when the load guard refused it). exec runs a command
line in the session (PUT /shell/session/<id>) and returns its result as JSON text: the value of
the reply, see text. Every command that fails raises Failure with the command, its output and the
log tail. The process runs in its own process group, which kill (and leaving a with block on an
error) kills, so no app keeps running. The output of the process goes to the log file, which is
kept.

exec adds member paths to the get and set commands of the shell, which take a whole component:
"get e Position.y" gets Position and returns its y member, get also takes elements as in
"Inventory.slots[0].amount". "set e View.shadows.enabled false" runs
"set e View {shadows: {enabled: false}}", which assigns only that member. The component is the
path up to the first name that starts with an upper case letter, or a longer one when that is
not a component (the mut of a template, as in Showcase.mut.clicks), the members follow it.
"""
import json
import os
import re
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


def parse_value(text):
    """Parse the output of exec, which is JSON text, into Python values."""
    return json.loads(text)


def text(value):
    """The output of exec for a result: empty for no result, else the result as JSON."""
    return "" if value is None else json.dumps(value)


def split_member(path):
    """Split a component path with members, such as box.app.AppStatus.frame or
    oreelse.OreInventory.slots[0].amount, into the component (box.app.AppStatus) and its member
    names and element indices (["frame"], ["slots", 0, "amount"]): the component ends at the first
    name that starts with an upper case letter."""
    splits = split_members(path)
    return splits[0] if splits else (path, [])


def split_members(path):
    """Every way to split a component path into a component and its members, see split_member,
    the shortest component first: a component can be a child of another, such as the mut of a
    template (Showcase.mut.clicks)."""
    names = path.split(".")
    first = next((i for i, name in enumerate(names) if name[:1].isupper()), None)
    result = []
    for i in range(first if first is not None else len(names), len(names)):
        if "[" in names[i]:
            break
        members = []
        for member in names[i + 1:]:
            for key, index in MEMBER.findall(member):
                members.append(key if key else int(index))
        result.append((".".join(names[:i + 1]), members))
    return result


MEMBER = re.compile(r"(\w+)|\[(\d+)\]")
UNRESOLVED = re.compile(r"unresolved identifier|parameter 'component'|is not a component")
GET_MEMBER = re.compile(r"^get\s+(\S+)\s+([A-Za-z_][\w.\[\]]*)\s*$")
SET_MEMBER = re.compile(r"^set\s+(\S+)\s+([A-Za-z_][\w.]*)\s+(.+)$", re.S)


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
    """A headless app process with a shell session on its REST port."""

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
                    status, body = self.request("shell/session", method="PUT")
                    if status == 200 and body:
                        self.session = json.loads(body)["session"]
                        return self
                    self.fail("/shell/session answered %d" % status, "shell/session", body)
                except (urllib.error.URLError, ConnectionError, socket.timeout):
                    pass
                if time.time() > limit:
                    self.fail("the port did not answer within %d s" % START_TIMEOUT,
                              "shell/session", "")
                time.sleep(0.05)
        except BaseException:
            self.kill()
            raise

    def url(self, path, params=None):
        query = "?" + urllib.parse.urlencode(params, quote_via=urllib.parse.quote) \
            if params else ""
        return "http://127.0.0.1:%d/%s%s" % (self.port, path, query)

    def request(self, path, params=None, binary=False, method="GET", data=None):
        """Send a request to the REST port: the status code and the body."""
        req = urllib.request.Request(self.url(path, params), method=method,
                                     data=data.encode() if data is not None else None)
        try:
            with urllib.request.urlopen(req, timeout=REQUEST_TIMEOUT) as r:
                status, body = r.status, r.read()
        except urllib.error.HTTPError as e:
            status, body = e.code, e.read()
        return status, body if binary else body.decode(errors="replace")

    def running(self):
        return self.proc is not None and self.proc.poll() is None

    def run(self, cmd):
        """Run a command line in the session as the shell gets it and return the result of its
        last command as a Python value, None when it returned none; raise Failure when it fails."""
        if time.time() > self.deadline:
            self.fail("the case ran out of its %d s" % self.timeout, cmd, "")
        if not self.running():
            self.fail("the app is not running (exit %s)" % self.proc.poll(), cmd, "")
        try:
            status, body = self.request("shell/session/%s" % self.session, method="PUT",
                                        data=cmd)
        except (urllib.error.URLError, ConnectionError, socket.timeout) as e:
            self.fail("no answer: %s" % e, cmd, "")
        try:
            reply = json.loads(body)
        except ValueError:
            reply = None
        if status != 200 or not isinstance(reply, dict) or reply.get("errors"):
            errors = reply.get("errors") if isinstance(reply, dict) else None
            out = "\n".join(errors) if errors else body
            self.last_output = out
            self.fail("the command failed (%d)" % status, cmd, out)
        results = reply.get("results") or [{}]
<<<<<<< HEAD
        last = results[-1] if isinstance(results[-1], dict) else {}
        result = last.get("result")
        if last.get("type") == "flecs.meta.value" and isinstance(result, dict) and \
=======
        result = results[-1].get("result")
        if results[-1].get("type") == "flecs.meta.value" and isinstance(result, dict) and \
>>>>>>> a8b5c6219466fa3d8ac0e15354c128d562b40b74
                len(result) == 1:
            result = next(iter(result.values()))
        return result

    def member(self, component, members, value):
        """Get a member of a component value by its names and element indices."""
        for member in members:
            if isinstance(member, int):
                found = isinstance(value, list) and member < len(value)
            else:
                found = isinstance(value, dict) and member in value
            if not found:
                self.last_output = text(value)
                self.fail("%s has no member %s" % (component, member), None, text(value))
            value = value[member]
        return value

    def component(self, line, splits, member=None):
        """Run line with the component of each split of a member path in turn, the shortest
        first, until one runs and has the members: the component, its members and the result,
        or the member of the result when member is given. Only a component that does not
        resolve or lacks the members moves on to the next split; any other failure, or the
        failure of the first split when no split works, is raised."""
        first = None
        for component, members in splits:
            try:
                value = self.run(line % component)
                if member:
                    value = member(component, members, value)
                return component, members, value
            except Failure as e:
                if not UNRESOLVED.search(str(e.output or "")) and \
                        not str(e).startswith("%s has no member" % component):
                    raise
                first = first or e
        raise first

    def exec(self, cmd):
        """Run a command line in the session and return its result as JSON text, see text. get
        and set take member paths, see the module documentation."""
        self.last_command = cmd
        self.last_output = None
        get = GET_MEMBER.match(cmd)
        put = SET_MEMBER.match(cmd)
        if get and split_member(get.group(2))[1]:
            value = self.component("get %s %%s" % get.group(1), split_members(get.group(2)),
                                   self.member)[2]
        elif put and split_member(put.group(2))[1]:
            splits = []
            for component, members in split_members(put.group(2)):
                expr = put.group(3)
                for member in reversed(members):
                    expr = "{%s: %s}" % (member, expr)
                splits.append(("%s %s" % (component, expr), []))
            value = self.component("set %s %%s" % put.group(1), splits)[2]
        else:
            value = self.run(cmd)
        out = text(value)
        self.last_output = out
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
        return int(self.exec("get $ box.app.AppStatus.frame"))

    def wait_frames(self, frames):
        """Let a number of frames pass, to check that something does not happen."""
        end = self.frame() + frames
        while self.frame() < end:
            pass

    def wait_input(self, frames=INPUT_FRAMES):
        """Poll input_wait until the input queue is drained."""
        start = None
        while True:
            out = self.exec("input_wait")
            if out.strip() == "0":
                return
            frame = self.frame()
            start = frame if start is None else start
            if frame - start > frames:
                self.fail("the input queue is not drained after %d frames" % frames,
                          "input_wait", out)

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

    def edit(self, state=None):
        """Run the edit command: toggle the editor, or open it with "on" and close it with "off".
        Returns whether the editor is open now; the view follows in the next frame."""
        cmd = "edit" if state is None else "edit %s" % state
        out = self.exec(cmd)
        if out not in ("true", "false"):
            self.fail("unexpected output %r" % out, cmd, out)
        return out == "true"

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
        resource = parse_value(out)["name"]
        start = None
        while True:
            status, body = self.request("shell/session/%s/resource/%s" % (self.session, resource),
                                        binary=True)
            if status == 200 and body:
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
