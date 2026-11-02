#!/usr/bin/env python3
"""Unit tests for the member paths of box_client.py, and an end to end check of them with
apps/boxrun on test/assets/scenes/counter_template.flecs when a built boxrun is found (bake3
build apps/boxrun --local-env). Run with: python3 tools/test_box_client.py [--json report.json]"""
import datetime as dt
import json
import os
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import box_client
import integration

COUNTER_SCENE = "test/assets/scenes/counter_template.flecs"
GUARD_WAIT = 300
GUARD_PAUSE = 5


class FakeApp(box_client.App):
    """An App whose run answers from a table of command lines instead of a process: a value, or
    the error output of a failed command for an Exception."""

    def __init__(self, replies):
        super().__init__("boxrun")
        self.replies = replies
        self.ran = []

    def run(self, cmd):
        self.ran.append(cmd)
        reply = self.replies.get(cmd, Exception("unresolved identifier '%s'" % cmd))
        if isinstance(reply, Exception):
            self.last_output = str(reply)
            self.fail("the command failed (400)", cmd, str(reply))
        return reply


class MemberPathTests(unittest.TestCase):
    def test_get_template_mut(self):
        app = FakeApp({
            "get counter Counter": Exception("unresolved identifier 'Counter'"),
            "get counter Counter.mut": {"count": 0},
        })
        self.assertEqual(app.exec("get counter Counter.mut.count"), "0")

    def test_set_template_mut_unknown_member(self):
        # The first split sets the mut as a member of the template, which it is not
        app = FakeApp({
            "set counter Counter {mut: {count: 5}}":
                Exception("unknown member 'mut' for type 'Counter'"),
            "set counter Counter.mut {count: 5}": None,
        })
        self.assertEqual(app.exec("set counter Counter.mut.count 5"), "")
        self.assertEqual(app.ran, ["set counter Counter {mut: {count: 5}}",
                                   "set counter Counter.mut {count: 5}"])

    def test_set_template_mut_cannot_resolve_member(self):
        app = FakeApp({
            "set counter Counter {mut: {count: 5}}": Exception("cannot resolve member"),
            "set counter Counter.mut {count: 5}": None,
        })
        self.assertEqual(app.exec("set counter Counter.mut.count 5"), "")
        self.assertEqual(app.ran[-1], "set counter Counter.mut {count: 5}")

    def test_set_unrelated_error_raised(self):
        app = FakeApp({
            "set counter Counter {mut: {count: 5}}": Exception("invalid value for i32"),
            "set counter Counter.mut {count: 5}": None,
        })
        with self.assertRaises(box_client.Failure) as e:
            app.exec("set counter Counter.mut.count 5")
        self.assertEqual(e.exception.output, "invalid value for i32")
        self.assertEqual(app.ran, ["set counter Counter {mut: {count: 5}}"])

    def test_set_no_split_raises_first(self):
        app = FakeApp({
            "set counter Counter {mut: {count: 5}}":
                Exception("unknown member 'mut' for type 'Counter'"),
            "set counter Counter.mut {count: 5}": Exception("unresolved identifier 'mut'"),
        })
        with self.assertRaises(box_client.Failure) as e:
            app.exec("set counter Counter.mut.count 5")
        self.assertEqual(e.exception.command, "set counter Counter {mut: {count: 5}}")


def boxrun_binary():
    class Args:
        app = "apps/boxrun"
        cfg = None
    try:
        return integration.app_binary(Args, integration.local_env_dirs())[0]
    except Exception:
        return None


@unittest.skipUnless(boxrun_binary(), "no built apps/boxrun (bake3 build apps/boxrun --local-env)")
class BoxrunTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.app = box_client.App(boxrun_binary(), [COUNTER_SCENE],
                                  log=os.path.join(self.dir.name, "boxrun.log"))
        self.addCleanup(self.app.kill)
        deadline = time.monotonic() + GUARD_WAIT
        while True:
            try:
                self.app.start()
                break
            except box_client.Refused:
                if time.monotonic() > deadline:
                    self.skipTest("the load guard refused boxrun for %d s" % GUARD_WAIT)
                time.sleep(GUARD_PAUSE)

    def test_set_template_mut(self):
        self.assertEqual(self.app.exec("get counter Counter.mut.count"), "0")
        self.app.exec("set counter Counter.mut.count 5")
        self.assertEqual(self.app.exec("get counter Counter.mut.count"), "5")

    def test_set_unrelated_error_raised(self):
        # The second split, Counter.mut, resolves; its error is raised, not the first one
        with self.assertRaises(box_client.Failure) as e:
            self.app.exec("set counter Counter.mut.count \"five\"")
        self.assertEqual(e.exception.command, 'set counter Counter.mut {count: "five"}')
        self.assertIn("failed to cast value", e.exception.output)


class JsonResult(unittest.TextTestResult):
    def record(self, test, status):
        if not hasattr(self, "records"):
            self.records = []
        self.records.append({"suite": type(test).__name__, "case": test._testMethodName,
                             "status": status})

    def addSuccess(self, test):
        super().addSuccess(test)
        self.record(test, "pass")

    def addFailure(self, test, err):
        super().addFailure(test, err)
        self.record(test, "fail")

    def addError(self, test, err):
        super().addError(test, err)
        self.record(test, "fail")


def main():
    json_path = None
    argv = list(sys.argv)
    if "--json" in argv:
        i = argv.index("--json")
        json_path = argv[i + 1]
        del argv[i:i + 2]
    start = time.monotonic()
    runner = unittest.TextTestRunner(resultclass=JsonResult, verbosity=2)
    loader = unittest.defaultTestLoader
    suite = unittest.TestSuite([loader.loadTestsFromTestCase(MemberPathTests),
                                loader.loadTestsFromTestCase(BoxrunTests)])
    result = runner.run(suite)
    if json_path:
        records = sorted(getattr(result, "records", []), key=lambda r: r["case"])
        report = {
            "project": "box_client",
            "timestamp": dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "pass": sum(r["status"] == "pass" for r in records),
            "fail": sum(r["status"] == "fail" for r in records),
            "empty": 0,
            "elapsed": time.monotonic() - start,
            "tests": records,
        }
        with open(json_path, "w") as f:
            json.dump(report, f, indent=2)
    sys.exit(0 if result.wasSuccessful() else 1)


if __name__ == "__main__":
    main()
