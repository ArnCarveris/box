// Unit tests for tools/texgraph/model.js. Run with: node --test tools/test_texgraph.mjs
//
// The world state is what tools/texgraph/core.js polls from a box app over REST: the node kinds
// from graph.NodeKind, the reflected types, the Graph textures and the node entities.

import { test } from "node:test";
import assert from "node:assert/strict";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);
const M = require("./texgraph/model.js");

const T = "box.texture.";
const N = T + "nodes.";

const vec4 = { x: ["float"], y: ["float"], z: ["float"], w: ["float"] };

function kind(id, output, inputs, params) {
  const ins = inputs.map(([name, type, value]) => ({ name, type, fallback: !!value, value: value || null }));
  while (ins.length < 4) ins.push({ name: null, type: "Scalar", fallback: false, value: null });
  return M.kindOf("", {
    id, output, input_count: inputs.length, inputs: ins,
    param_count: params.length, params: params.map((p) => ({ name: p, offset: 0, size: 4 })),
    param_size: 16, function: "f", neighbourhood: false, animated: false
  });
}

function world() {
  const kinds = {
    [N + "Constant"]: kind(1, "Vector", [], ["value"]),
    [N + "Coords"]: kind(2, "Vector", [], ["scale"]),
    [N + "Mix"]: kind(3, "Vector", [["a", "Vector", "a_value"], ["b", "Vector", "b_value"], ["factor", "Vector", "factor_value"]], []),
    ["app.TgScalar"]: kind(4, "Scalar", [["in", "Vector", "in_value"]], ["value"]),
    [N + "Multiply"]: kind(5, "Vector", [["a", "Vector", "a_value"], ["b", "Vector", "b_value"]], [])
  };
  for (const c in kinds) kinds[c].comp = c;
  const types = {
    [N + "Constant"]: { value: vec4 },
    [N + "Coords"]: { scale: ["float"] },
    [N + "Mix"]: {
      a: ["entity"], b: ["entity"], factor: ["entity"], a_value: vec4, b_value: vec4, factor_value: vec4
    },
    [N + "Multiply"]: { a: ["entity"], b: ["entity"], a_value: vec4, b_value: vec4 },
    ["app.TgScalar"]: { value: ["float"], in: ["entity"], in_value: vec4 },
    [T + "Graph"]: {
      output: ["entity"], width: ["int"], height: ["int"],
      domain: ["enum", "DomainPlane", "DomainSphere"],
      format: ["enum", "GraphColor", "GraphLinear", "GraphSingle", "GraphHdr"]
    }
  };
  const node = (path, comp, value) => ({
    path, name: M.leaf(path), parent: M.parentOf(path), kinds: [comp], values: { [comp]: value }
  });
  const zero = { x: 0, y: 0, z: 0, w: 0 };
  const nodes = {};
  for (const n of [
    node("scene.tiles.uv", N + "Coords", { scale: 2 }),
    node("scene.tiles.warm", N + "Constant", { value: { x: 0.9, y: 0.5, z: 0.2, w: 1 } }),
    node("scene.tiles.blend", N + "Mix", { a: "scene.tiles.warm", b: "#0", factor: "scene.tiles.uv", a_value: zero, b_value: { x: 0.1, y: 0.3, z: 0.8, w: 1 }, factor_value: zero }),
    node("scene.tiles.dimmed", N + "Multiply", { a: "scene.tiles.blend", b: "#0", a_value: zero, b_value: { x: 0.8, y: 0.8, z: 0.8, w: 1 } }),
    node("scene.tiles.loose", "app.TgScalar", { value: 0.5, in: "#0", in_value: zero }),
    node("scene.shared", N + "Constant", { value: { x: 1, y: 1, z: 1, w: 1 } }),
    node("scene.other.n", N + "Coords", { scale: 0 })
  ]) nodes[n.path] = n;
  const textures = {
    "scene.tiles": {
      path: "scene.tiles", name: "tiles", parent: "scene",
      graph: { output: "scene.tiles.dimmed", width: 256, height: 0, domain: "DomainPlane", format: "GraphColor" },
      status: { state: "Ready", revision: 1, error: null }
    }
  };
  return { kinds, types, nodes, textures };
}

test("graph of a texture has its descendants and the edges of their inputs", () => {
  const s = world();
  const g = M.graphOf(s, "scene.tiles");
  assert.deepEqual([...g.nodes.keys()].sort(), [
    "scene.tiles.blend", "scene.tiles.dimmed", "scene.tiles.loose", "scene.tiles.uv", "scene.tiles.warm"
  ]);
  const edges = g.edges.map((e) => `${M.leaf(e.from)}>${M.leaf(e.to)}.${e.port}`).sort();
  assert.deepEqual(edges, ["blend>dimmed.a", "uv>blend.factor", "warm>blend.a"]);
  assert.equal(g.output, "scene.tiles.dimmed");
  assert.deepEqual(g.dangling, []);
});

test("graph reaches nodes outside the texture through inputs and lists missing ones", () => {
  const s = world();
  s.nodes["scene.tiles.dimmed"].values[N + "Multiply"].b = "scene.shared";
  s.nodes["scene.tiles.blend"].values[N + "Mix"].b = "scene.gone";
  const g = M.graphOf(s, "scene.tiles");
  assert.ok(g.nodes.has("scene.shared"));
  assert.ok(!g.nodes.has("scene.other.n"));
  assert.deepEqual(g.dangling, [{ to: "scene.tiles.blend", port: "b", ref: "scene.gone" }]);
});

test("connections of the wrong type and cycles are refused", () => {
  const s = world();
  assert.equal(M.connectError(s, "scene.tiles.uv", "scene.tiles.dimmed", "b"), null);
  assert.match(M.connectError(s, "scene.tiles.loose", "scene.tiles.dimmed", "b"), /scalar/);
  assert.match(M.connectError(s, "scene.tiles.dimmed", "scene.tiles.blend", "b"), /cycle/);
  assert.match(M.connectError(s, "scene.tiles.blend", "scene.tiles.blend", "b"), /cycle/);
  assert.match(M.connectError(s, "scene.tiles.uv", "scene.tiles.dimmed", "nope"), /no input/);
  assert.equal(M.connectError(s, "scene.tiles.blend", "@output", "output"), null);
  assert.match(M.connectError(s, "scene.tiles.loose", "@output", "output"), /scalar/);
});

test("only nodes the output of a graph reaches have a value to preview", () => {
  const s = world();
  assert.ok(M.isReached(s, "scene.tiles.dimmed"));
  assert.ok(M.isReached(s, "scene.tiles.warm"));
  assert.ok(!M.isReached(s, "scene.tiles.loose"));
  s.nodes["scene.tiles.dimmed"].values[N + "Multiply"].b = "scene.shared";
  assert.ok(M.isReached(s, "scene.shared"));
  s.textures["scene.tiles"].graph.output = "#0";
  assert.ok(!M.isReached(s, "scene.tiles.warm"));
});

test("the nodes an error names are found", () => {
  const err = "texture graph 'scene.tiles': node 'scene.tiles.loose' outputs Scalar but input " +
    "'b' of node 'scene.tiles.dimmed' expects Vector";
  assert.deepEqual(M.errorNodes(err), ["scene.tiles.loose", "scene.tiles.dimmed"]);
  assert.deepEqual(M.errorNodes(null), []);
});

test("an error marks the nodes it names and the inputs that refer to a named entity", () => {
  const s = world();
  s.nodes["scene.tiles.dimmed"].values[N + "Multiply"].b = "scene.camera";
  const g = M.graphOf(s, "scene.tiles");
  assert.deepEqual(M.errorTargets(g, "texture graph 'scene.tiles': node 'scene.camera' has no node component"),
    ["scene.tiles.dimmed"]);
  assert.deepEqual(M.errorTargets(g, "node 'scene.tiles.blend' failed"), ["scene.tiles.blend"]);
});

test("script reproduces the graph with inputs before the nodes that use them", () => {
  const s = world();
  const text = M.toScript(s, "scene.tiles");
  const lines = text.split("\n");
  assert.ok(lines.includes("using box.texture"));
  assert.ok(lines.includes("tiles {"));
  assert.ok(lines.includes("  uv { nodes.Coords: {scale: 2} }"));
  assert.ok(lines.includes("  warm { nodes.Constant: {value: {0.9, 0.5, 0.2, 1}} }"));
  assert.ok(lines.includes("  blend { nodes.Mix: {a: warm, factor: uv, b_value: {0.1, 0.3, 0.8, 1}} }"));
  assert.ok(lines.includes("  dimmed { nodes.Multiply: {a: blend, b_value: {0.8, 0.8, 0.8, 1}} }"));
  assert.ok(lines.includes("  loose { app.TgScalar: {value: 0.5} }"));
  assert.ok(lines.includes("  Graph: {output: dimmed, width: 256}"));
  const at = (name) => lines.findIndex((l) => l.startsWith(`  ${name} `));
  assert.ok(at("warm") < at("blend") && at("uv") < at("blend") && at("blend") < at("dimmed"));
  assert.equal(lines[lines.length - 2], "}");
});

test("script refers to nodes outside the texture by path and keeps non default enums", () => {
  const s = world();
  s.nodes["scene.tiles.dimmed"].values[N + "Multiply"].b = "scene.shared";
  s.textures["scene.tiles"].graph.domain = "DomainSphere";
  s.textures["scene.tiles"].graph.format = "GraphHdr";
  const text = M.toScript(s, "scene.tiles");
  assert.match(text, /\/\/ uses scene\.shared/);
  assert.match(text, /dimmed \{ nodes\.Multiply: \{a: blend, b: scene\.shared, /);
  assert.match(text, /Graph: \{output: dimmed, width: 256, domain: DomainSphere, format: GraphHdr\}/);
});

test("values serialize to flecs script", () => {
  const ref = (p) => p;
  assert.equal(M.scriptValue(0.1 + 0.2, ["float"], ref), "0.3");
  assert.equal(M.scriptValue(3, ["int"], ref), "3");
  assert.equal(M.scriptValue(true, ["bool"], ref), "true");
  assert.equal(M.scriptValue("a \"b\"", ["text"], ref), "\"a \\\"b\\\"\"");
  assert.equal(M.scriptValue({ x: 0, y: 0, z: 0, w: 0 }, vec4, ref), "{}");
  const key = { position: ["float"], color: { r: ["int"], g: ["int"], b: ["int"] } };
  assert.equal(M.scriptValue([{ position: 0, color: { r: 10, g: 20, b: 30 } }, { position: 1, color: { r: 255, g: 255, b: 255 } }],
    ["vector", key], ref), "[{color: {10, 20, 30}}, {position: 1, color: {255, 255, 255}}]");
});

test("node members write zero where their default is not zero and leave out the default", () => {
  const ref = (p) => p;
  const coords = { scale: ["float"] };
  const vec3 = { x: ["float"], y: ["float"], z: ["float"] };
  const transform = { coords: ["entity"], angle_value: ["float"], scale: vec3 };
  const time = { scale: ["float"], offset: ["float"] };
  assert.equal(M.scriptValue({ scale: 0 }, coords, ref, M.nodeDefaults[N + "Coords"]), "{scale: 0}");
  assert.equal(M.scriptValue({ scale: 1 }, coords, ref, M.nodeDefaults[N + "Coords"]), "{}");
  assert.equal(M.scriptValue({ scale: 0, offset: 3 }, time, ref, M.nodeDefaults[N + "Time"]),
    "{scale: 0, offset: 3}");
  assert.equal(M.scriptValue({ coords: 0, angle_value: 0, scale: { x: 1, y: 1, z: 1 } }, transform, ref,
    M.nodeDefaults[N + "Transform"]), "{}");
  assert.equal(M.scriptValue({ coords: 0, angle_value: 90, scale: { x: 0, y: 1, z: 1 } }, transform, ref,
    M.nodeDefaults[N + "Transform"]), "{angle_value: 90, scale: {0, 1, 1}}");
  assert.equal(M.scriptValue({ scale: 0 }, null, ref, M.nodeDefaults[N + "Coords"]), "{scale: 0}");
});

test("new nodes get a name no entity has", () => {
  const s = world();
  s.nodes["scene.tiles.mix_1"] = s.nodes["scene.tiles.blend"];
  assert.equal(M.uniqueName(s, "scene.tiles", N + "Mix"), "scene.tiles.mix_2");
  assert.equal(M.uniqueName(s, "scene.tiles", N + "Constant"), "scene.tiles.constant_1");
  assert.equal(M.uniqueName(s, "scene.tiles", "app.TgScalar"), "scene.tiles.tgScalar_1");
});

test("colours convert between linear and display encoding", () => {
  assert.equal(M.hex(255, 128, 0), "#ff8000");
  assert.deepEqual(M.unhex("#ff8000"), [255, 128, 0]);
  for (const c of [0, 0.002, 0.2, 0.5, 1]) assert.ok(Math.abs(M.fromSrgb(M.toSrgb(c)) - c) < 1e-6);
});
