"use strict";

// Pure functions of the texture graph editor: the graph of a texture from the polled world,
// connection checks and the flecs script that reproduces a graph. No DOM, so node tests load it.

(function (root) {
  const TEX = "box.texture.";

  const M = {};

  M.TEX = TEX;

  M.isNone = (ref) => ref === undefined || ref === null || ref === 0 || ref === "#0" || ref === "0" || ref === "";

  M.leaf = (path) => {
    if (!path) return "";
    const i = path.lastIndexOf(".");
    return i < 0 ? path : path.slice(i + 1);
  };

  M.parentOf = (path) => {
    const i = path ? path.lastIndexOf(".") : -1;
    return i < 0 ? "" : path.slice(0, i);
  };

  M.short = (comp) => comp && comp.startsWith(TEX) ? comp.slice(TEX.length) : comp;

  M.under = (path, scope) => !!scope && !!path && path.startsWith(scope + ".");

  // The kind of a graph.NodeKind value as the editor uses it: the inputs and parameters that
  // are set, by name.
  M.kindOf = (comp, k) => {
    const inputs = (k.inputs || []).slice(0, k.input_count || 0).map((p) => ({
      name: p.name,
      type: p.type,
      fallback: !!p.fallback,
      value: p.value || null
    }));
    const params = (k.params || []).slice(0, k.param_count || 0).map((p) => ({
      name: p.name, offset: p.offset, size: p.size
    }));
    return {
      comp,
      id: k.id,
      output: k.output,
      inputs,
      params,
      fn: k.function,
      neighbourhood: !!k.neighbourhood,
      animated: !!k.animated
    };
  };

  // The node component of a node: the first of its components that is a node kind.
  M.nodeKind = (state, node) => {
    for (const c of node.kinds || []) if (state.kinds[c]) return state.kinds[c];
    return null;
  };

  M.nodeValue = (node) => node && node.kinds && node.kinds.length ? node.values[node.kinds[0]] : undefined;

  // The graph of a texture: its descendants with a node component and every node its output
  // reaches through the inputs, the edges from the inputs and the inputs whose node is missing.
  M.graphOf = (state, texPath) => {
    const tex = state.textures[texPath];
    const nodes = new Map();
    const edges = [];
    const dangling = [];
    if (!tex) return { tex: null, nodes, edges, dangling, output: null };
    const graph = tex.graph || {};
    const visit = (path) => {
      if (nodes.has(path)) return;
      const n = state.nodes[path];
      if (!n) return;
      nodes.set(path, n);
      const kind = M.nodeKind(state, n);
      const v = M.nodeValue(n) || {};
      if (!kind) return;
      for (const p of kind.inputs) if (!M.isNone(v[p.name])) visit(v[p.name]);
    };
    for (const path of Object.keys(state.nodes).sort()) {
      if (M.under(path, texPath)) visit(path);
    }
    if (!M.isNone(graph.output)) visit(graph.output);
    for (const n of nodes.values()) {
      const kind = M.nodeKind(state, n);
      const v = M.nodeValue(n) || {};
      if (!kind) continue;
      kind.inputs.forEach((p, i) => {
        const ref = v[p.name];
        if (M.isNone(ref)) return;
        if (nodes.has(ref)) edges.push({ from: ref, to: n.path, port: p.name, index: i, type: p.type });
        else dangling.push({ to: n.path, port: p.name, ref });
      });
    }
    const output = M.isNone(graph.output) ? null : graph.output;
    return { tex, nodes, edges, dangling, output };
  };

  // Whether the node at "from" is reached from "to" through the inputs, so connecting from
  // into to would close a cycle.
  M.reaches = (state, to, from) => {
    const seen = new Set();
    const stack = [from];
    while (stack.length) {
      const path = stack.pop();
      if (path === to) return true;
      if (seen.has(path)) continue;
      seen.add(path);
      const n = state.nodes[path];
      const kind = n ? M.nodeKind(state, n) : null;
      const v = M.nodeValue(n) || {};
      if (!kind) continue;
      for (const p of kind.inputs) if (!M.isNone(v[p.name])) stack.push(v[p.name]);
    }
    return false;
  };

  // Whether the output of a texture graph reaches a node through the inputs: only such a node
  // has a value the engine can read back for its preview.
  M.isReached = (state, path) => Object.values(state.textures).some((t) =>
    t.graph && !M.isNone(t.graph.output) && M.reaches(state, path, t.graph.output));

  // Why the output of node "from" cannot go into input "port" of node "to", null when it can.
  // The pseudo node "@output" is the output of the graph, which takes a vector.
  M.connectError = (state, from, to, port) => {
    const src = state.nodes[from];
    const srcKind = src ? M.nodeKind(state, src) : null;
    if (!srcKind) return "the source is not a node";
    let expect;
    if (to === "@output") {
      expect = "Vector";
    } else {
      const dst = state.nodes[to];
      const dstKind = dst ? M.nodeKind(state, dst) : null;
      const p = dstKind ? dstKind.inputs.find((x) => x.name === port) : null;
      if (!p) return `no input '${port}'`;
      expect = p.type;
      if (from === to || M.reaches(state, to, from)) {
        return "the connection would make a cycle";
      }
    }
    // An Any port takes or gives either type; the engine checks the types it resolves to.
    if (srcKind.output !== expect && srcKind.output !== "Any" && expect !== "Any") {
      return `${M.leaf(from)} outputs ${M.typeLabel(srcKind.output)}, ${port} takes ${M.typeLabel(expect)}`;
    }
    return null;
  };

  M.typeLabel = (t) => t === "Scalar" ? "a scalar" : t === "Vector" ? "a vector" : String(t);

  // The node paths a Status error names: "node 'a.b'" and "of node 'a.b'".
  M.errorNodes = (error) => {
    const out = [];
    if (!error) return out;
    const re = /node '([^']+)'/g;
    let m;
    while ((m = re.exec(error))) if (out.indexOf(m[1]) < 0) out.push(m[1]);
    return out;
  };

  // The nodes of a graph an error is about: the ones it names and the ones with an input that
  // refers to an entity it names, such as an entity without a node component.
  M.errorTargets = (g, error) => {
    const named = M.errorNodes(error);
    const out = named.filter((p) => g.nodes.has(p));
    for (const e of g.edges.concat(g.dangling.map((d) => ({ from: d.ref, to: d.to })))) {
      if (named.includes(e.from) && !g.nodes.has(e.from) && !out.includes(e.to)) out.push(e.to);
    }
    return out;
  };

  M.isZero = (v, ti) => {
    if (v === undefined || v === null) return true;
    if (Array.isArray(ti)) {
      const k = ti[0];
      if (k === "entity") return M.isNone(v);
      if (k === "enum") return ti.length > 1 && v === ti[1];
      if (k === "vector") return !v.length;
      if (k === "array") return Array.isArray(v) && v.every((x) => M.isZero(x, ti[1]));
      if (k === "text") return !v;
      return !v;
    }
    if (ti && typeof ti === "object" && typeof v === "object") {
      return Object.keys(ti).filter((k) => k !== "@self").every((k) => M.isZero(v[k], ti[k]));
    }
    if (typeof v === "object") return Object.values(v).every((x) => M.isZero(x));
    return !v;
  };

  M.num = (v) => {
    if (typeof v !== "number") return String(v);
    if (Number.isInteger(v)) return String(v);
    return String(+v.toPrecision(7));
  };

  // The members of node components whose ctor gives them a value other than zero. A script that
  // leaves such a member out gets that value, so the script writes them when they differ from it,
  // zero included.
  M.nodeDefaults = {
    [TEX + "nodes.Coords"]: { scale: 1 },
    [TEX + "nodes.Time"]: { scale: 1 },
    [TEX + "nodes.Transform"]: { scale: { x: 1, y: 1, z: 1 } },
    [TEX + "nodes.Normal"]: { strength: 1 },
    [TEX + "nodes.Slope"]: { strength: 1 },
    [TEX + "nodes.Noise"]: { frequency: 4, octaves: 1, lacunarity: 2, gain: 0.5, offset: 1, channels: 1 },
    [TEX + "nodes.Blocks"]: { courses: 8, per_course: 4, softness: -1 },
    [TEX + "nodes.Cells"]: { count: 64, jitter: 1, edge: 0.25 },
  };

  // Whether a member value equals its default value.
  M.sameValue = (v, d) => {
    if (d && typeof d === "object") {
      return Object.keys(d).every((k) => M.sameValue((v || {})[k], d[k]));
    }
    return (v === undefined || v === null ? 0 : v) === d;
  };

  // A value in flecs script syntax, members that are zero left out of structs, or with defaults
  // (M.nodeDefaults of the component) members equal to their default. ref maps an entity path to
  // the name the script uses for it.
  M.scriptValue = (v, ti, ref, defaults) => {
    const kind = Array.isArray(ti) ? ti[0] : (ti && typeof ti === "object" ? "struct" : null);
    if (kind === "entity") return M.isNone(v) ? "0" : ref(v);
    if (kind === "enum" || kind === "bitmask") return String(v);
    if (kind === "float" || kind === "int" || kind === "byte") return M.num(v);
    if (kind === "bool") return v ? "true" : "false";
    if (kind === "text") return v === null || v === undefined ? '""' : JSON.stringify(String(v));
    if (kind === "array" || kind === "vector") {
      return "[" + (v || []).map((x) => M.scriptValue(x, ti[1], ref)).join(", ") + "]";
    }
    if (kind === "struct") {
      const keys = Object.keys(ti).filter((k) => k !== "@self");
      const flat = ["x,y", "x,y,z", "x,y,z,w", "r,g,b", "r,g,b,a"].includes(keys.join()) &&
        keys.every((k) => Array.isArray(ti[k]) && ["float", "int", "byte"].includes(ti[k][0]));
      if (flat && (defaults || !M.isZero(v, ti))) {
        return "{" + keys.map((k) => M.num((v || {})[k] || 0)).join(", ") + "}";
      }
      const parts = [];
      for (const k of keys) {
        const d = defaults ? defaults[k] : undefined;
        if (d === undefined ? M.isZero((v || {})[k], ti[k]) : M.sameValue((v || {})[k], d)) continue;
        parts.push(`${k}: ${M.scriptValue((v || {})[k], ti[k], ref, d && typeof d === "object" ? d : undefined)}`);
      }
      return "{" + parts.join(", ") + "}";
    }
    if (Array.isArray(v)) return "[" + v.map((x) => M.scriptValue(x, null, ref)).join(", ") + "]";
    if (v && typeof v === "object") {
      const parts = [];
      for (const k in v) {
        const d = defaults ? defaults[k] : undefined;
        if (d === undefined ? M.isZero(v[k]) : M.sameValue(v[k], d)) continue;
        parts.push(`${k}: ${M.scriptValue(v[k], null, ref)}`);
      }
      return "{" + parts.join(", ") + "}";
    }
    if (typeof v === "number") return M.num(v);
    if (typeof v === "boolean") return v ? "true" : "false";
    if (typeof v === "string") return /^[A-Za-z_][\w.]*$/.test(v) ? v : JSON.stringify(v);
    return "0";
  };

  // The name of a component in the script, which uses box.texture.
  M.scriptComp = (comp) => M.short(comp);

  // Order the paths so the inputs of a node come before it.
  M.topological = (state, paths) => {
    const set = new Set(paths);
    const out = [];
    const seen = new Set();
    const visit = (path) => {
      if (seen.has(path) || !set.has(path)) return;
      seen.add(path);
      const n = state.nodes[path];
      const kind = n ? M.nodeKind(state, n) : null;
      const v = M.nodeValue(n) || {};
      if (kind) for (const p of kind.inputs) if (!M.isNone(v[p.name])) visit(v[p.name]);
      out.push(path);
    };
    paths.forEach(visit);
    return out;
  };

  // The graph of a texture as flecs script: the texture entity named as its leaf, its nodes
  // as children in the order of their inputs and its Graph. Nodes outside the texture
  // are referred to by their full path and listed in a comment.
  M.toScript = (state, texPath) => {
    const g = M.graphOf(state, texPath);
    if (!g.tex) return "";
    const ref = (path) => M.under(path, texPath) ? path.slice(texPath.length + 1) : path;
    const inside = [...g.nodes.keys()].filter((p) => M.under(p, texPath));
    const outside = [...g.nodes.keys()].filter((p) => !M.under(p, texPath));
    const lines = [];
    lines.push(`// Texture graph ${texPath}, written by tools/texgraph`);
    for (const p of outside) lines.push(`// uses ${p}, which is defined outside the graph`);
    lines.push("");
    lines.push("using box.texture");
    lines.push("");
    lines.push(`${M.leaf(texPath)} {`);
    const tree = { children: new Map(), path: texPath };
    for (const p of M.topological(state, inside)) {
      const parts = p.slice(texPath.length + 1).split(".");
      let t = tree;
      let cur = texPath;
      for (const part of parts) {
        cur += "." + part;
        if (!t.children.has(part)) t.children.set(part, { children: new Map(), path: cur });
        t = t.children.get(part);
      }
    }
    const emit = (t, indent) => {
      for (const [name, child] of t.children) {
        const n = state.nodes[child.path];
        const comps = n ? n.kinds.map((c) => `${M.scriptComp(c)}: ${M.scriptValue(n.values[c], state.types[c], ref,
          M.nodeDefaults[c])}`) : [];
        if (!child.children.size && comps.length <= 1) {
          lines.push(comps.length ? `${indent}${name} { ${comps[0]} }` : `${indent}${name} {}`);
          continue;
        }
        lines.push(`${indent}${name} {`);
        for (const c of comps) lines.push(`${indent}  ${c}`);
        emit(child, indent + "  ");
        lines.push(`${indent}}`);
      }
    };
    emit(tree, "  ");
    const graphType = state.types[TEX + "Graph"];
    lines.push(`  Graph: ${M.scriptValue(g.tex.graph || {}, graphType || null, ref)}`);
    lines.push("}");
    lines.push("");
    return lines.join("\n");
  };

  // A name for a new node of a kind in a texture that no entity has.
  M.uniqueName = (state, texPath, comp) => {
    const base = M.leaf(comp).replace(/^[A-Z]/, (c) => c.toLowerCase()) || "node";
    let i = 1;
    while (state.nodes[`${texPath}.${base}_${i}`] || (state.names && state.names.has(`${texPath}.${base}_${i}`))) i++;
    return `${texPath}.${base}_${i}`;
  };

  // Linear light to a display encoded channel and back, for colour pickers of vec4 values.
  M.toSrgb = (c) => {
    c = Math.max(0, Math.min(1, c));
    return c <= 0.0031308 ? c * 12.92 : 1.055 * Math.pow(c, 1 / 2.4) - 0.055;
  };

  M.fromSrgb = (c) => c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4);

  M.hex = (r, g, b) => "#" + [r, g, b].map((x) => Math.round(Math.max(0, Math.min(255, x)))
    .toString(16).padStart(2, "0")).join("");

  M.unhex = (hex) => {
    const m = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(hex || "");
    return m ? [parseInt(m[1], 16), parseInt(m[2], 16), parseInt(m[3], 16)] : [0, 0, 0];
  };

  if (typeof module !== "undefined" && module.exports) module.exports = M;
  else root.TGM = M;
})(typeof window !== "undefined" ? window : globalThis);
