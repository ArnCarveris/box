#!/usr/bin/env node
/* Unit tests for the inspector page: transports, reply parsing, session and
   console history. The code under test is the <script id="inspector-core">
   block of index.html, which exports itself when a CommonJS module is around.

   Usage: node test.js [--json report.json] */
"use strict";

const fs = require("fs");
const path = require("path");
const vm = require("vm");

const ROOT = __dirname;
const PROJECT = "box_inspector";

function loadCore() {
  const html = fs.readFileSync(path.join(ROOT, "index.html"), "utf8");
  const match = /<script id="inspector-core">([\s\S]*?)<\/script>/.exec(html);
  if (!match) {
    throw new Error("index.html has no inspector-core script");
  }
  const sandbox = { module: { exports: {} }, console, Promise, URL };
  sandbox.exports = sandbox.module.exports;
  vm.createContext(sandbox);
  vm.runInContext(match[1], sandbox, { filename: "index.html#inspector-core" });
  return sandbox.module.exports;
}

const I = loadCore();

/* ---------- stubs ---------- */

function stubFetch(routes) {
  const calls = [];
  const fetch = (url, init) => {
    calls.push({ url, init: init || {} });
    const route = routes[url] || routes["*"];
    if (!route) {
      return Promise.reject(new Error("no stub for " + url));
    }
    const type = route.type || "text/plain";
    return Promise.resolve({
      status: route.status === undefined ? 200 : route.status,
      headers: { get: (name) => (name.toLowerCase() === "content-type" ? type : null) },
      text: () => Promise.resolve(route.body === undefined ? "" : route.body),
      blob: () => Promise.resolve({ stub: "blob", body: route.body })
    });
  };
  fetch.calls = calls;
  return fetch;
}

function stubBoxRequest(reply) {
  const calls = [];
  const request = (method, requestPath, body) => {
    calls.push({ method, path: requestPath, body });
    return typeof reply === "function" ? reply(method, requestPath, body) : reply;
  };
  request.calls = calls;
  return request;
}

const objectUrls = [];
const io = (fetch) => ({
  fetch,
  createObjectURL: (blob) => {
    objectUrls.push(blob);
    return "blob:stub/" + objectUrls.length;
  }
});

/* ---------- test runner ---------- */

const results = [];
let suite = "";
const cases = [];

function describe(name, fn) {
  suite = name;
  fn();
}

function test(name, fn) {
  cases.push({ suite, name, fn });
}

function assert(condition, message) {
  if (!condition) {
    throw new Error(message || "assertion failed");
  }
}

function equal(actual, expected, message) {
  const a = JSON.stringify(actual);
  const b = JSON.stringify(expected);
  assert(a === b, (message || "value") + ": expected " + b + ", got " + a);
}

/* ---------- reply parsing ---------- */

describe("Reply", () => {
  test("parses_status_type_and_body", () => {
    const reply = I.parseReply("200\ntext/plain\ns1");
    equal(reply.status, 200, "status");
    equal(reply.type, "text/plain", "type");
    equal(reply.body, "s1", "body");
  });

  test("keeps_newlines_in_the_body", () => {
    const reply = I.parseReply("200\ntext/plain\nline one\nline two\n");
    equal(reply.body, "line one\nline two\n", "body");
  });

  test("error_status_survives", () => {
    equal(I.parseReply("400\ntext/plain\nunknown command").status, 400, "status");
  });

  test("malformed_reply_is_text", () => {
    const reply = I.parseReply("garbage");
    equal(reply.status, 0, "status");
    equal(reply.body, "garbage", "body");
  });

  test("base64_image_becomes_a_data_url", () => {
    equal(I.dataUrl("iVBORw0KG\ngo="), "data:image/png;base64,iVBORw0KGgo=", "url");
  });
});

/* ---------- result colouring ---------- */

function has(text, needle, message) {
  assert(String(text).indexOf(needle) >= 0,
    (message || "html") + ": expected " + JSON.stringify(needle) +
    " in " + JSON.stringify(text));
}

describe("Colors", () => {
  test("ls_splits_entities_from_types", () => {
    equal(I.colorize("camera\nPosition\n", "ls"),
      "<span class=\"t-path\">camera</span>\n<span class=\"t-type\">Position</span>",
      "ls output");
  });

  test("cat_colors_the_component_and_its_value", () => {
    const html = I.colorize("Position: {x: 1, y: -2.5}\nPrefab", "cat camera");
    has(html, "<span class=\"t-type\">Position</span>:", "component name");
    has(html, "<span class=\"t-key\">x</span>", "member name");
    has(html, "<span class=\"t-num\">1</span>", "number");
    has(html, "<span class=\"t-num\">-2.5</span>", "negative number");
    has(html, "<span class=\"t-type\">Prefab</span>", "bare tag");
  });

  test("a_qualified_component_keeps_its_namespace_quiet", () => {
    const html = I.colorize("box.transform.Position: {x: 0}\n(flecs.script.Script,scene)",
      "cat camera");
    has(html, "<span class=\"t-key\">box.transform.</span><span class=\"t-type\">Position</span>:",
      "qualified name");
    has(html, "<span class=\"t-type\">(flecs.script.Script,scene)</span>", "pair");
  });

  test("strings_keep_their_own_color", () => {
    const html = I.colorize("Doc: {value: \"scene root\"}", "get box Doc");
    has(html, "<span class=\"t-str\">\"scene root\"</span>", "string value");
  });

  test("help_shows_parameters_and_brief", () => {
    const html = I.colorize("cat <path:string> [component:string] - Show components", "help");
    has(html, "<span class=\"t-cmd\">cat</span>", "command name");
    has(html, "&lt;<span class=\"t-key\">path</span>:<span class=\"t-type\">string</span>&gt;",
      "required parameter");
    has(html, "[<span class=\"t-key\">component</span>:<span class=\"t-type\">string</span>]",
      "optional parameter");
    has(html, "<span class=\"t-brief\">Show components</span>", "brief");
  });

  test("tab_columns_become_an_aligned_grid", () => {
    const html = I.colorize("box.camera\tPosition\t{x: 0, y: 4}\nbox.light\tPosition\t{x: 1}\n",
      "grep Position");
    has(html, "<div class=\"cols\" style=\"grid-template-columns: max-content max-content " +
      "minmax(0, 1fr)\">", "grid template");
    has(html, "<span class=\"col\"><span class=\"t-path\">box.camera</span></span>", "path cell");
    has(html, "<span class=\"col\"><span class=\"t-type\">Position</span></span>", "type cell");
    has(html, "<span class=\"t-num\">4</span>", "value cell");
    equal(html.split("<span class=\"col\">").length - 1, 6, "cell count");
  });

  test("log_levels_are_colored_by_severity", () => {
    const html = I.colorize("error\tcould not open file\nwarning\tslow frame\ninfo\tready\n",
      "logs");
    has(html, "<span class=\"t-lvl t-lvl-error\">error</span>", "error level");
    has(html, "<span class=\"t-lvl t-lvl-warning\">warning</span>", "warning level");
    has(html, "<span class=\"t-lvl t-lvl-info\">info</span>", "info level");
    has(html, "<span class=\"col\">could not open file</span>", "message stays plain");
  });

  test("errors_pass_through_unstyled", () => {
    equal(I.colorize("error: unknown command 'nope'", "nope"),
      "error: unknown command 'nope'", "error line");
    equal(I.colorize("error: '<a>' is not a valid path", "cd <a>"),
      "error: '&lt;a&gt;' is not a valid path", "escaped, no spans");
  });

  test("the_prompt_bolds_the_command_name", () => {
    equal(I.colorize("$ cat camera", "cat camera"),
      "<span class=\"t-prompt\">$</span> <span class=\"t-cmd\">cat</span> camera", "prompt");
  });

  test("paths_share_one_color", () => {
    equal(I.colorize("box.scene.camera", "pwd"),
      "<span class=\"t-path\">box.scene.camera</span>", "path");
    equal(I.colorize("/", "cd /"), "<span class=\"t-path\">/</span>", "root");
  });

  test("markup_in_a_value_is_escaped", () => {
    const html = I.colorize("Doc: {value: \"a < b & <img src=x onerror=boom>\"}", "cat box");
    has(html, "a &lt; b &amp; &lt;img src=x onerror=boom&gt;", "escaped value");
    assert(html.indexOf("<img") < 0, "no raw markup");
    assert(html.indexOf("<b ") < 0, "no raw tag");
  });

  test("a_leading_blank_line_is_dropped", () => {
    equal(I.colorize("\napps\nbox\n", "ls"),
      "<span class=\"t-path\">apps</span>\n<span class=\"t-path\">box</span>", "ls at the root");
  });

  test("empty_output_stays_empty", () => {
    equal(I.colorize("", "ls"), "", "empty");
    equal(I.colorize(null, "ls"), "", "null");
    equal(I.colorize("\n\n", "ls"), "", "blank lines");
  });
});

/* ---------- http transport ---------- */

describe("Http", () => {
  test("text_reply_carries_status_and_body", () => {
    const fetch = stubFetch({ "http://host:27750/terminal/new": { body: "s1" } });
    return I.httpTransport("http://host:27750", io(fetch))
      .request("GET", "/terminal/new", "")
      .then((reply) => {
        equal(reply.status, 200, "status");
        equal(reply.body, "s1", "body");
        equal(reply.image, null, "image");
      });
  });

  test("trailing_slash_is_trimmed", () => {
    const fetch = stubFetch({ "*": { body: "s1" } });
    return I.httpTransport("http://host:27750//", io(fetch))
      .request("GET", "/terminal/new", "")
      .then(() => equal(fetch.calls[0].url, "http://host:27750/terminal/new", "url"));
  });

  test("png_reply_becomes_an_object_url", () => {
    const fetch = stubFetch({ "*": { type: "image/png", body: "binary" } });
    return I.httpTransport("http://host:27750", io(fetch))
      .request("GET", "/capture?session=s1", "")
      .then((reply) => {
        equal(reply.type, "image/png", "type");
        assert(/^blob:stub\//.test(reply.image), "image is an object url");
      });
  });

  test("error_status_is_reported", () => {
    const fetch = stubFetch({ "*": { status: 400, body: "unknown command 'nope'" } });
    return I.httpTransport("http://host:27750", io(fetch))
      .request("GET", "/terminal/exec?session=s1&cmd=nope", "")
      .then((reply) => {
        equal(reply.status, 400, "status");
        equal(reply.body, "unknown command 'nope'", "body");
      });
  });

  test("body_is_posted_as_text_plain", () => {
    const fetch = stubFetch({ "*": { body: "ok" } });
    return I.httpTransport("http://host:27750", io(fetch))
      .request("POST", "/terminal/exec?session=s1", "set a Position {1, 2, 3}")
      .then(() => {
        equal(fetch.calls[0].init.method, "POST", "method");
        equal(fetch.calls[0].init.headers["Content-Type"], "text/plain", "content type");
        equal(fetch.calls[0].init.body, "set a Position {1, 2, 3}", "body");
      });
  });
});

/* ---------- wasm transport ---------- */

describe("Wasm", () => {
  test("routes_method_path_and_body", () => {
    const request = stubBoxRequest("200\ntext/plain\nok");
    return I.wasmTransport(request).request("GET", "/terminal/new", "").then(() => {
      equal(request.calls[0], { method: "GET", path: "/terminal/new", body: "" }, "call");
    });
  });

  test("text_reply_is_parsed", () => {
    const request = stubBoxRequest("200\ntext/plain\ns1");
    return I.wasmTransport(request).request("GET", "/terminal/new", "").then((reply) => {
      equal(reply.status, 200, "status");
      equal(reply.body, "s1", "body");
    });
  });

  test("png_reply_becomes_a_data_url", () => {
    const request = stubBoxRequest("200\nimage/png\niVBORw0KGgo=");
    return I.wasmTransport(request).request("GET", "/capture?session=s1", "").then((reply) => {
      equal(reply.type, "image/png", "type");
      equal(reply.image, "data:image/png;base64,iVBORw0KGgo=", "image");
    });
  });

  test("unknown_endpoint_gives_404", () => {
    const request = stubBoxRequest("404\ntext/plain\nunknown endpoint");
    return I.wasmTransport(request).request("GET", "/nope", "").then((reply) => {
      equal(reply.status, 404, "status");
    });
  });

  test("module_name_comes_from_the_url", () => {
    equal(I.wasmModuleName("http://host/db/command/wasm/box_app.js"), "box_app", "name");
    equal(I.wasmArguments(" etc/scenes/primitives.flecs "),
      ["etc/scenes/primitives.flecs"], "arguments");
  });
});

/* ---------- endpoint paths ---------- */

describe("Paths", () => {
  test("exec_uses_get_with_an_encoded_command", () => {
    const req = I.execRequest("s1", "set camera Position {0, 4, -12}");
    equal(req.method, "GET", "method");
    equal(req.path, "/terminal/exec?session=s1&cmd=" +
      encodeURIComponent("set camera Position {0, 4, -12}"), "path");
    equal(req.body, "", "body");
  });

  test("long_command_falls_back_to_post", () => {
    const line = "x".repeat(I.POST_THRESHOLD + 1);
    const req = I.execRequest("s1", line);
    equal(req.method, "POST", "method");
    equal(req.path, "/terminal/exec?session=s1", "path");
    equal(req.body, line, "body");
  });

  test("bare_clear_is_handled_by_the_page", () => {
    equal(I.localCommand("clear"), "clear", "bare clear");
    equal(I.localCommand("  clear  "), "clear", "padded clear");
    equal(I.localCommand("clear camera"), null, "clear with an argument goes to the engine");
    equal(I.localCommand("ls"), null, "other commands go to the engine");
  });

  test("capture_takes_an_optional_camera", () => {
    equal(I.capturePath("s1", ""), "/capture?session=s1", "without camera");
    equal(I.capturePath("s1", "scene.cam"), "/capture?session=s1&camera=scene.cam", "with camera");
  });

  test("resource_lines_are_detected", () => {
    equal(I.resourcePaths("capture_12_0.png\ncapture_12_1.png\n"),
      ["capture_12_0.png", "capture_12_1.png"], "capture output");
    equal(I.resourcePaths("camera\nok"), [], "plain output");
    equal(I.resourcePaths("wrote a file to shot.png"), [], "line with spaces");
    equal(I.resourcePaths("shots/live.PNG\nnotes.txt"), ["shots/live.PNG"], "nested path");
    equal(I.resourcePaths("../secret.png"), [], "path that escapes the folder");
  });

  test("resource_path_carries_the_session", () => {
    equal(I.resourcePath("s1", "capture_12_0.png"),
      "/resource/capture_12_0.png?session=s1", "path");
  });

  test("resource_is_retried_until_it_is_ready", () => {
    const replies = ["404\ntext/plain\nresource not found", "404\ntext/plain\nresource not found",
      "200\nimage/png\niVBORw0KGgo="];
    const request = stubBoxRequest(() => replies.shift());
    const waits = [];
    return I.fetchResource(I.wasmTransport(request), "s1", "capture_12_0.png",
      { attempts: 5, delay: 500, wait: (ms) => { waits.push(ms); return Promise.resolve(); } })
      .then((reply) => {
        equal(request.calls.length, 3, "requests");
        equal(waits, [500, 500], "retry delays");
        equal(reply.image, "data:image/png;base64,iVBORw0KGgo=", "image");
      });
  });

  test("resource_gives_up_after_the_last_attempt", () => {
    const request = stubBoxRequest("404\ntext/plain\nresource not found");
    return I.fetchResource(I.wasmTransport(request), "s1", "capture_12_0.png",
      { attempts: 3, delay: 0, wait: () => Promise.resolve() })
      .then((reply) => {
        equal(request.calls.length, 3, "requests");
        equal(reply.status, 404, "status");
        equal(reply.image, null, "no image");
      });
  });

  test("frame_is_read_from_get_app", () => {
    equal(I.parseFrame("{headless: true, frames: 0, frame: 421, port: 27750}"), 421, "frame");
    equal(I.parseFrame("Error: no such entity"), null, "no frame");
  });
});

/* ---------- session ---------- */

describe("Session", () => {
  test("connect_creates_a_session", () => {
    const request = stubBoxRequest("200\ntext/plain\ns2");
    const session = I.createSession(I.wasmTransport(request));
    return session.open().then((name) => {
      equal(name, "s2", "name");
      equal(session.name(), "s2", "stored name");
    });
  });

  test("failed_new_is_an_error", () => {
    const session = I.createSession(I.wasmTransport(stubBoxRequest("503\ntext/plain\nno command server")));
    return session.open().then(
      () => assert(false, "open should reject"),
      (err) => equal(err.message, "no command server", "message"));
  });

  test("exec_runs_on_the_session", () => {
    const request = stubBoxRequest((method, requestPath) =>
      requestPath === "/terminal/new" ? "200\ntext/plain\ns1" : "200\ntext/plain\nhelp output");
    const session = I.createSession(I.wasmTransport(request));
    return session.open().then(() => session.exec("help")).then((reply) => {
      equal(reply.body, "help output", "body");
      equal(request.calls[1].path, "/terminal/exec?session=s1&cmd=help", "path");
    });
  });

  test("disconnect_closes_the_session", () => {
    const request = stubBoxRequest("200\ntext/plain\ns1");
    const session = I.createSession(I.wasmTransport(request));
    return session.open().then(() => session.close()).then(() => {
      equal(request.calls[1].path, "/terminal/close?session=s1", "path");
      equal(session.name(), null, "name cleared");
    });
  });
});

/* ---------- console ---------- */

describe("Console", () => {
  test("up_walks_back_and_down_walks_forward", () => {
    const history = I.createHistory(["ls", "get App"], 10);
    equal(history.prev(), "get App", "first up");
    equal(history.prev(), "ls", "second up");
    equal(history.prev(), "ls", "stops at the oldest");
    equal(history.next(), "get App", "first down");
    equal(history.next(), "", "down past the newest clears");
    equal(history.next(), null, "nothing left");
  });

  test("push_resets_the_cursor_and_skips_repeats", () => {
    const history = I.createHistory([], 10);
    history.push("ls");
    history.push("ls");
    history.push("");
    equal(history.items(), ["ls"], "items");
    history.push("get App");
    equal(history.prev(), "get App", "cursor at the end");
  });

  test("history_is_capped", () => {
    const history = I.createHistory([], 3);
    ["a", "b", "c", "d"].forEach((line) => history.push(line));
    equal(history.items(), ["b", "c", "d"], "items");
  });

  test("history_survives_a_broken_store", () => {
    const broken = { getItem: () => { throw new Error("blocked"); }, setItem: () => { throw new Error("blocked"); } };
    const store = I.createStore(broken, "box-inspector");
    equal(store.load(), {}, "load");
    store.save({ history: ["ls"] });
  });

  test("store_round_trips", () => {
    const memory = {};
    const store = I.createStore({
      getItem: (key) => (key in memory ? memory[key] : null),
      setItem: (key, value) => { memory[key] = value; }
    }, "box-inspector");
    store.save({ mode: "wasm", history: ["ls", "get App"] });
    equal(store.load(), { mode: "wasm", history: ["ls", "get App"] }, "round trip");
  });
});

/* ---------- query parameters ---------- */

describe("Query", () => {
  test("http_parameter_preconnects", () => {
    const query = I.parseQuery("?http=http%3A%2F%2Flocalhost%3A27750");
    equal(query.mode, "http", "mode");
    equal(query.endpoint, "http://localhost:27750", "endpoint");
    equal(query.connect, true, "connect");
  });

  test("wasm_parameter_carries_a_scene", () => {
    const query = I.parseQuery("?wasm=/db/command/wasm/box_app.js&scene=etc/scenes/primitives.flecs");
    equal(query.mode, "wasm", "mode");
    equal(query.image, "/db/command/wasm/box_app.js", "image");
    equal(query.scene, "etc/scenes/primitives.flecs", "scene");
  });

  test("no_parameters_means_no_preconnect", () => {
    equal(I.parseQuery("").connect, false, "connect");
    equal(I.parseQuery("?other=1").mode, null, "mode");
  });
});

/* ---------- tab completion ---------- */

describe("Completion", () => {
  test("command_names_are_filtered_by_prefix", () => {
    equal(I.completeCommand(["cat", "cd", "clear", "logs"], "c"), ["cat", "cd", "clear"], "matches");
    equal(I.completeCommand(["cat", "cd", "clear"], "ca"), ["cat"], "single match");
  });

  test("no_prefix_match_is_an_empty_list", () => {
    equal(I.completeCommand(["cat", "cd"], "zz"), [], "candidates");
  });

  test("several_matches_cycle_forward_and_wrap", () => {
    const candidates = I.completeCommand(["cat", "cd", "clear"], "c");
    let index = I.cycle(candidates, -1, true);
    equal(candidates[index], "cat", "first tab");
    index = I.cycle(candidates, index, true);
    equal(candidates[index], "cd", "second tab");
    index = I.cycle(candidates, index, true);
    equal(candidates[index], "clear", "third tab");
    index = I.cycle(candidates, index, true);
    equal(candidates[index], "cat", "wraps forward");
  });

  test("shift_tab_cycles_backwards", () => {
    const candidates = ["cat", "cd", "clear"];
    const index = I.cycle(candidates, 0, false);
    equal(candidates[index], "clear", "wraps backward from the first entry");
  });

  test("cycle_of_an_empty_list_is_negative_one", () => {
    equal(I.cycle([], 0, true), -1, "empty");
  });

  test("path_splits_at_the_last_unescaped_dot", () => {
    equal(I.splitPath("box.scene.cam"), { parent: "box.scene", partial: "cam" }, "plain dots");
    equal(I.splitPath("box\\.scene.cam"), { parent: "box\\.scene", partial: "cam" }, "escaped dot in parent");
    equal(I.splitPath("box\\.scene"), { parent: "", partial: "box\\.scene" }, "escaped dot is not a separator");
    equal(I.splitPath("cam"), { parent: "", partial: "cam" }, "no dot at all");
  });

  test("children_are_completed_under_a_scope", () => {
    const children = ["camera", "cube", "scene"];
    const parts = I.splitPath("box.ca");
    equal(parts, { parent: "box", partial: "ca" }, "scope and partial");
    equal(I.completePath(children, parts.partial), ["camera"], "single child match");
  });

  test("escaped_child_names_are_matched_and_kept_escaped", () => {
    const children = ["light\\.rig", "light2"];
    equal(I.completePath(children, "light\\."), ["light\\.rig"], "matches the escaped form");
    equal(I.completePath(children, "light"), ["light\\.rig", "light2"], "both children");
  });
});

/* ---------- run ---------- */

async function run() {
  const started = process.hrtime.bigint();
  for (const item of cases) {
    const at = process.hrtime.bigint();
    let status = "pass";
    let error = null;
    try {
      await item.fn();
    } catch (err) {
      status = "fail";
      error = err;
    }
    const elapsed = Number(process.hrtime.bigint() - at) / 1e9;
    results.push({ suite: item.suite, case: item.name, status, elapsed: Math.round(elapsed * 1e6) / 1e6 });
    if (error) {
      console.log("FAIL: " + item.suite + "." + item.name + ": " + error.message);
    }
  }

  const pass = results.filter((r) => r.status === "pass").length;
  const fail = results.length - pass;
  const report = {
    project: PROJECT,
    timestamp: new Date().toISOString().replace(/\.\d+Z$/, "Z"),
    pass,
    fail,
    empty: 0,
    elapsed: Math.round(Number(process.hrtime.bigint() - started) / 1e3) / 1e6,
    tests: results
  };

  const flag = process.argv.indexOf("--json");
  if (flag >= 0 && process.argv[flag + 1]) {
    const out = path.resolve(process.argv[flag + 1]);
    fs.mkdirSync(path.dirname(out), { recursive: true });
    fs.writeFileSync(out, JSON.stringify(report, null, 2) + "\n");
    console.log("wrote " + out);
  }

  console.log("Pass: " + pass + ", Fail: " + fail + " (" + report.elapsed + "s)");
  process.exit(fail ? 1 : 0);
}

run();
