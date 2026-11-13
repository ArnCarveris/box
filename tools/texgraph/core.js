"use strict";

// Connection to a box app over the flecs REST API: polls the node kinds, texture graphs, nodes,
// previews and statistics, writes components coalesced per entity and component (as DAW.write
// in tools/daw/core.js) and keeps TextureNodePreview on the nodes the graph view shows.

const TEX = TGM.TEX;

const TG = window.TG = {
  conn: null,
  host: null,
  connected: false,
  hasTexture: false,
  epoch: 0,
  kinds: {},
  types: {},
  typesDirty: true,
  nodes: {},
  textures: {},
  previews: {},
  stats: null,
  tex: localStorage.getItem("texgraph.tex") || null,
  sel: null,
  pending: {},
  previewSet: new Map(),
  listeners: [],
  dragging: 0,
  sig: "",
  names: new Set(),
  deleting: new Set()
};

TG.on = (fn) => TG.listeners.push(fn);
TG.emit = (what, arg) => { for (const fn of TG.listeners) fn(what, arg); };

TG.toast = (msg, err) => {
  const el = document.getElementById("toast");
  el.textContent = msg;
  el.className = "show" + (err ? " err" : "");
  clearTimeout(TG.toastTimer);
  TG.toastTimer = setTimeout(() => { el.className = ""; }, err ? 5000 : 2200);
};

TG.clone = (v) => v === undefined ? v : JSON.parse(JSON.stringify(v));

const pathOf = (r) => r.parent ? r.parent + "." + r.name : r.name;

function query(expr, params) {
  return new Promise((resolve, reject) => {
    const conn = TG.conn;
    if (!conn) { reject("no connection"); return; }
    conn.query(expr, Object.assign({ values: true }, params || {}), (r) => resolve(r), (e) => reject(e));
  });
}

TG.request = (method, path, params) => new Promise((resolve, reject) => {
  const conn = TG.conn;
  if (!conn) { reject("no connection"); return; }
  conn._.request(conn, method, path, params || {},
    (msg) => resolve(msg),
    (msg) => {
      let text = msg;
      try { text = JSON.parse(msg).error || msg; } catch (e) { }
      reject(text);
    });
});

TG.entityPath = (path) => TG.conn._.escapePath(path);

function getComponent(comp) {
  return TG.request("GET", "component/" + TG.entityPath(comp), { component: comp })
    .then((msg) => { try { return JSON.parse(msg); } catch (e) { return undefined; } }, () => undefined);
}

TG.valueOf = (path, comp) => {
  if (TG.nodes[path] && TG.nodes[path].values[comp] !== undefined) return TG.nodes[path].values[comp];
  if (TG.textures[path] && comp === TEX + "TextureGraph") return TG.textures[path].graph;
  return undefined;
};

function applyLocal(path, comp, partial) {
  const n = TG.nodes[path];
  if (n && n.values[comp]) Object.assign(n.values[comp], TG.clone(partial));
  const t = TG.textures[path];
  if (t && comp === TEX + "TextureGraph" && t.graph) Object.assign(t.graph, TG.clone(partial));
}

TG.write = (path, comp, partial) => {
  if (!TG.conn || !path) return;
  const key = path + "|" + comp;
  let w = TG.pending[key];
  if (!w) w = TG.pending[key] = { path, comp, value: {}, recent: {}, inflight: false, t: 0 };
  Object.assign(w.value, TG.clone(partial));
  Object.assign(w.recent, TG.clone(partial));
  w.t = performance.now();
  applyLocal(path, comp, partial);
  flush(w);
};

function flush(w) {
  if (w.inflight || !Object.keys(w.value).length) return;
  const v = w.value;
  w.value = {};
  w.inflight = true;
  const epoch = TG.epoch;
  TG.request("PUT", "component/" + TG.entityPath(w.path), {
    component: w.comp,
    value: encodeURIComponent(JSON.stringify(v))
  }).then(() => {
    w.inflight = false;
    w.t = performance.now();
    if (epoch === TG.epoch) flush(w);
  }, (err) => {
    w.inflight = false;
    TG.toast(`set ${TGM.short(w.comp)} on ${w.path} failed:\n${err}`, true);
    if (epoch === TG.epoch) flush(w);
  });
}

function overlayPending() {
  const now = performance.now();
  for (const key in TG.pending) {
    const w = TG.pending[key];
    if (!w.inflight && now - w.t > 700 && !Object.keys(w.value).length) {
      delete TG.pending[key];
      continue;
    }
    applyLocal(w.path, w.comp, w.recent);
  }
}

TG.isPending = (path, comp) => {
  const w = TG.pending[path + "|" + comp];
  return !!w && (w.inflight || performance.now() - w.t < 700);
};

TG.add = (path, comp, value) => TG.request("PUT", "component/" + TG.entityPath(path),
  value === undefined ? { component: comp } : { component: comp, value: encodeURIComponent(JSON.stringify(value)) });
TG.removeComp = (path, comp) => TG.request("DELETE", "component/" + TG.entityPath(path), { component: comp });
TG.create = (path) => TG.request("PUT", "entity/" + TG.entityPath(path), {});
TG.del = (path) => TG.request("DELETE", "entity/" + TG.entityPath(path), {});

function signature() {
  const parts = [TG.tex || ""];
  for (const c of Object.keys(TG.kinds).sort()) parts.push("k" + c);
  for (const path of Object.keys(TG.textures).sort()) {
    const t = TG.textures[path];
    parts.push("t" + path, t.graph ? t.graph.output : "", t.graph ? t.graph.domain : "",
      t.status ? t.status.state + (t.status.error || "") : "");
  }
  for (const path of Object.keys(TG.nodes).sort()) {
    const n = TG.nodes[path];
    parts.push("n" + path, n.kinds.join("+"));
    const kind = TGM.nodeKind(TG, n);
    const v = TGM.nodeValue(n) || {};
    if (kind) for (const p of kind.inputs) parts.push(p.name, String(v[p.name]));
  }
  return parts.join(",");
}

async function pollKinds(needTypes) {
  const r = await query(TEX + "TextureNodeKind", { type_info: needTypes });
  const kinds = {};
  for (const res of r.results || []) {
    const v = res.fields && res.fields.values ? res.fields.values[0] : null;
    if (v && typeof v === "object") kinds[pathOf(res)] = TGM.kindOf(pathOf(res), v);
  }
  return kinds;
}

async function pollTextures(needTypes) {
  const r = await query(`${TEX}TextureGraph, ?${TEX}TextureStatus, ?${TEX}Texture`, { type_info: needTypes });
  if (r.type_info) for (const c in r.type_info) if (r.type_info[c]) TG.types[c] = r.type_info[c];
  const textures = {};
  for (const res of r.results || []) {
    const vals = res.fields && res.fields.values ? res.fields.values : [];
    const obj = (x) => x && typeof x === "object" ? x : null;
    textures[pathOf(res)] = {
      path: pathOf(res), name: res.name, parent: res.parent,
      graph: obj(vals[0]) || {}, status: obj(vals[1]), size: obj(vals[2])
    };
  }
  return textures;
}

async function pollNodes(kinds, needTypes) {
  const comps = Object.keys(kinds);
  const replies = await Promise.all(comps.map((c) =>
    query(c, { type_info: needTypes || !TG.types[c] }).catch(() => null)));
  const nodes = {};
  replies.forEach((r, i) => {
    if (!r) return;
    const comp = comps[i];
    if (r.type_info) for (const c in r.type_info) if (r.type_info[c]) TG.types[c] = r.type_info[c];
    for (const res of r.results || []) {
      const path = pathOf(res);
      const v = res.fields && res.fields.values ? res.fields.values[0] : undefined;
      let n = nodes[path];
      if (!n) n = nodes[path] = { path, name: res.name, parent: res.parent, kinds: [], values: {} };
      n.kinds.push(comp);
      n.values[comp] = v && typeof v === "object" ? v : {};
    }
  });
  for (const n of Object.values(nodes)) n.kinds.sort((a, b) => kinds[a].id - kinds[b].id);
  return nodes;
}

async function pollStructure(epoch) {
  if (epoch !== TG.epoch) return;
  if (TG.connected) {
    try {
      const needTypes = TG.typesDirty;
      const kinds = await pollKinds(needTypes);
      if (epoch !== TG.epoch) return;
      const [textures, nodes, stats] = await Promise.all([
        pollTextures(needTypes), pollNodes(kinds, needTypes), getComponent(TEX + "TextureGraphStats")
      ]);
      if (epoch !== TG.epoch) return;
      TG.kinds = kinds;
      TG.textures = textures;
      TG.nodes = nodes;
      TG.hasTexture = Object.keys(kinds).length > 0;
      if (stats) {
        TG.statsPrev = TG.stats ? { stats: TG.stats, t: TG.statsTime } : null;
        TG.stats = stats;
        TG.statsTime = performance.now();
      }
      TG.typesDirty = Object.keys(kinds).some((c) => !TG.types[c]) || !TG.types[TEX + "TextureGraph"];
      overlayPending();
      if (TG.tex && !TG.textures[TG.tex]) TG.tex = null;
      if (!TG.tex) {
        const first = Object.keys(TG.textures).sort()[0];
        if (first) TG.selectTexture(first, true);
      }
      if (TG.sel && TG.sel !== TG.tex && !TG.nodes[TG.sel]) TG.select(null);
      const sig = signature();
      const changed = sig !== TG.sig;
      TG.sig = sig;
      TG.emit(changed ? "structure" : "values");
      TG.emit("stats");
    } catch (e) {
      console.error(e);
    }
  }
  setTimeout(() => pollStructure(epoch), 900);
}

async function pollPreviews(epoch) {
  if (epoch !== TG.epoch) return;
  if (TG.connected && TG.previewSet.size) {
    try {
      const r = await query(TEX + "TextureNodePreviewStatus");
      if (epoch !== TG.epoch) return;
      const changed = [];
      const seen = new Set();
      for (const res of r.results || []) {
        const path = pathOf(res);
        const v = res.fields && res.fields.values ? res.fields.values[0] : null;
        if (!v || typeof v !== "object") continue;
        seen.add(path);
        const prev = TG.previews[path];
        if (!prev || prev.revision !== v.revision || prev.width !== v.width || prev.height !== v.height) {
          TG.previews[path] = { revision: v.revision, width: v.width, height: v.height, src: v.png ? "data:image/png;base64," + v.png : "" };
          changed.push(path);
        }
      }
      for (const path of Object.keys(TG.previews)) {
        if (!seen.has(path)) {
          delete TG.previews[path];
          changed.push(path);
        }
      }
      if (changed.length) TG.emit("previews", changed);
    } catch (e) {
      console.error(e);
    }
  }
  setTimeout(() => pollPreviews(epoch), 250);
}

// Set TextureNodePreview on the nodes of a map from path to size and remove it from the nodes
// the editor previewed before that are not in it.
TG.syncPreviews = (wanted) => {
  if (!TG.conn || !TG.connected) return;
  for (const [path, size] of wanted) {
    if (TG.previewSet.get(path) === size || !TG.nodes[path] || TG.deleting.has(path)) continue;
    TG.previewSet.set(path, size);
    TG.add(path, TEX + "TextureNodePreview", { size, rate: 4 }).catch((e) => {
      TG.previewSet.delete(path);
      console.warn("preview of " + path + ": " + e);
    });
  }
  for (const path of [...TG.previewSet.keys()]) {
    if (wanted.has(path)) continue;
    TG.previewSet.delete(path);
    if (TG.nodes[path]) TG.removeComp(path, TEX + "TextureNodePreview").catch(() => { });
  }
};

TG.releasePreviews = (sync) => {
  if (!TG.conn) return;
  for (const path of TG.previewSet.keys()) {
    const url = TG.conn.url + "/component/" + TG.entityPath(path) + "?component=" + encodeURIComponent(TEX + "TextureNodePreview");
    if (sync && typeof fetch === "function") fetch(url, { method: "DELETE", keepalive: true }).catch(() => { });
    else TG.removeComp(path, TEX + "TextureNodePreview").catch(() => { });
  }
  TG.previewSet.clear();
};

TG.selectTexture = (path, quiet) => {
  if (path === TG.tex) return;
  TG.releasePreviews(false);
  TG.previews = {};
  TG.tex = path;
  TG.sel = null;
  if (path) localStorage.setItem("texgraph.tex", path);
  if (!quiet) TG.emit("structure");
};

TG.select = (path) => {
  TG.sel = path;
  TG.emit("select");
};

TG.graph = () => TGM.graphOf(TG, TG.tex);

TG.kindOfNode = (path) => {
  const n = TG.nodes[path];
  return n ? TGM.nodeKind(TG, n) : null;
};

TG.connect = (host) => {
  TG.disconnect();
  TG.host = host;
  TG.epoch++;
  const epoch = TG.epoch;
  TG.typesDirty = true;
  localStorage.setItem("texgraph.host", host);
  TG.conn = flecs.connect({
    host,
    poll_interval_ms: 1000,
    on_status: (status) => {
      if (epoch !== TG.epoch) return;
      TG.connected = status === flecs.ConnectionStatus.Connected;
      TG.emit("connection");
    }
  });
  pollStructure(epoch);
  pollPreviews(epoch);
  TG.emit("connection");
};

TG.disconnect = () => {
  if (TG.conn) {
    TG.releasePreviews(true);
    TG.conn.disconnect();
  }
  TG.epoch++;
  TG.conn = null;
  TG.connected = false;
  TG.hasTexture = false;
  TG.kinds = {};
  TG.nodes = {};
  TG.textures = {};
  TG.previews = {};
  TG.stats = null;
  TG.statsPrev = null;
  TG.pending = {};
  TG.sig = "";
  TG.sel = null;
  TG.emit("structure");
};

// Create a node of a kind in the selected texture, named after the kind.
TG.createNode = async (comp) => {
  if (!TG.tex) return null;
  const path = TGM.uniqueName(TG, TG.tex, comp);
  TG.names.add(path);
  try {
    await TG.create(path);
    await TG.add(path, comp);
    TG.nodes[path] = { path, name: TGM.leaf(path), parent: TG.tex, kinds: [comp], values: { [comp]: {} } };
    TG.emit("structure");
    return path;
  } catch (e) {
    TG.toast(`create ${TGM.short(comp)} failed:\n${e}`, true);
    return null;
  }
};

// Delete nodes, first disconnecting the inputs and the graph output that refer to them, so the
// graphs do not keep references to deleted entities. The disconnects are written and answered
// before the delete, so no graph sees a missing node in between.
TG.deleteNodes = async (paths) => {
  const gone = new Set(paths);
  const clears = [];
  for (const path of paths) TG.deleting.add(path);
  const clear = (path, comp, partial) => {
    applyLocal(path, comp, partial);
    clears.push(TG.add(path, comp, partial).catch((e) => TG.toast(`disconnect ${path} failed:\n${e}`, true)));
  };
  for (const n of Object.values(TG.nodes)) {
    if (gone.has(n.path)) continue;
    const kind = TGM.nodeKind(TG, n);
    const v = TGM.nodeValue(n) || {};
    if (!kind) continue;
    const partial = {};
    for (const p of kind.inputs) if (gone.has(v[p.name])) partial[p.name] = "#0";
    if (Object.keys(partial).length) clear(n.path, n.kinds[0], partial);
  }
  for (const t of Object.values(TG.textures)) {
    if (t.graph && gone.has(t.graph.output)) clear(t.path, TEX + "TextureGraph", { output: "#0" });
  }
  await Promise.all(clears);
  for (const path of paths) {
    TG.previewSet.delete(path);
    delete TG.previews[path];
    try {
      await TG.del(path);
      delete TG.nodes[path];
    } catch (e) {
      TG.toast(`delete ${path} failed:\n${e}`, true);
    }
  }
  for (const path of paths) TG.deleting.delete(path);
  if (gone.has(TG.sel)) TG.sel = null;
  TG.emit("structure");
};

// Connect the output of a node to an input, or to the output of the graph with "@output".
TG.connectPorts = (from, to, port) => {
  const err = TGM.connectError(TG, from, to, port);
  if (err) {
    TG.toast("cannot connect: " + err, true);
    return false;
  }
  if (to === "@output") TG.write(TG.tex, TEX + "TextureGraph", { output: from });
  else TG.write(to, TG.nodes[to].kinds[0], { [port]: from });
  TG.emit("structure");
  return true;
};

TG.disconnectPort = (to, port) => {
  if (to === "@output") TG.write(TG.tex, TEX + "TextureGraph", { output: "#0" });
  else TG.write(to, TG.nodes[to].kinds[0], { [port]: "#0" });
  TG.emit("structure");
};

TG.statsRates = () => {
  const s = TG.stats, p = TG.statsPrev;
  if (!s || !p) return null;
  const dt = (TG.statsTime - p.t) / 1000;
  if (dt <= 0) return null;
  return {
    evaluations: (s.evaluations - p.stats.evaluations) / dt,
    reads: (s.reads - p.stats.reads) / dt,
    gpu_ms: s.timed > p.stats.timed ? (s.gpu_ms - p.stats.gpu_ms) / (s.timed - p.stats.timed) : null
  };
};

window.addEventListener("beforeunload", () => TG.releasePreviews(true));
