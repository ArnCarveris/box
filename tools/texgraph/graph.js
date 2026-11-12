"use strict";

// The graph view: a node per node entity with its kind, a thumbnail and its input ports, the
// edges of the inputs and the output of the graph as a sink node. Zoom, drag, marquee selection
// and auto arrange follow tools/daw/graph.js; ports connect by dragging.

const G = window.G = {
  zoom: parseFloat(localStorage.getItem("texgraph.zoom") || "1"),
  edgeEls: [],
  picked: new Set(),
  selEdge: null
};

const GPAD = 24, GCOLW = 230, GNODEW = 176, GGAP = 18, GHEAD = 32, GROW = 18, GTHUMB = 120;

const SINK = "@output";

G.el = (tag, attrs, ...children) => {
  const el = document.createElement(tag);
  if (attrs) {
    for (const k in attrs) {
      const v = attrs[k];
      if (v === undefined || v === null || v === false) continue;
      if (k === "class") el.className = v;
      else if (k === "style" && typeof v === "object") Object.assign(el.style, v);
      else if (k.startsWith("on")) el.addEventListener(k.slice(2), v);
      else if (k === "text") el.textContent = v;
      else el.setAttribute(k, v === true ? "" : v);
    }
  }
  for (const c of children.flat()) {
    if (c === undefined || c === null || c === false) continue;
    el.appendChild(typeof c === "string" || typeof c === "number" ? document.createTextNode(c) : c);
  }
  return el;
};

G.color = (name) => {
  let h = 0;
  for (const c of name || "") h = (h * 31 + c.charCodeAt(0)) >>> 0;
  return `hsl(${h % 360}, 55%, 58%)`;
};

G.typeClass = (t) => t === "TexScalar" ? "t-scalar" : "t-vector";

G.menu = (x, y, items) => {
  G.closeMenu();
  const m = G.el("div", { class: "menu" });
  for (const it of items) {
    if (!it) { m.appendChild(G.el("div", { class: "msep" })); continue; }
    if (it.title) { m.appendChild(G.el("div", { class: "mtitle" }, it.title)); continue; }
    m.appendChild(G.el("div", { title: it.doc, onclick: () => { G.closeMenu(); it.action(); } }, it.label));
  }
  document.body.appendChild(m);
  const r = m.getBoundingClientRect();
  m.style.left = Math.max(0, Math.min(x, window.innerWidth - r.width - 4)) + "px";
  m.style.top = Math.max(0, Math.min(y, window.innerHeight - r.height - 4)) + "px";
  G.openMenu = m;
  setTimeout(() => document.addEventListener("pointerdown", G.menuAway, true), 0);
};

G.menuAway = (e) => {
  if (G.openMenu && !G.openMenu.contains(e.target)) G.closeMenu();
};

G.closeMenu = () => {
  if (G.openMenu) G.openMenu.remove();
  G.openMenu = null;
  document.removeEventListener("pointerdown", G.menuAway, true);
};

// Height of the thumbnails of a texture: the aspect of its image, an equirectangular 2:1 for
// the sphere domain.
G.thumbH = (tex) => {
  const g = tex && tex.graph ? tex.graph : {};
  const w = g.width || 512, h = g.height || w;
  const aspect = g.domain === "DomainSphere" && !g.height ? 0.5 : h / w;
  return Math.round(Math.max(40, Math.min(GTHUMB * 1.4, (GNODEW - 12) * aspect)));
};

G.model = () => {
  const g = TG.graph();
  const nodes = new Map();
  const thumb = G.thumbH(g.tex);
  for (const n of g.nodes.values()) {
    const kind = TGM.nodeKind(TG, n);
    const inputs = kind ? kind.inputs : [];
    nodes.set(n.path, {
      id: n.path, path: n.path, rec: n, kind, title: n.name,
      sub: n.kinds.map(TGM.short).join(" + ") + (kind ? (kind.output === "TexScalar" ? " · scalar" : "") : " · not a node"),
      inputs, h: GHEAD + thumb + inputs.length * GROW + 6, thumb, ins: [], outs: [], order: n.path
    });
  }
  if (g.tex) {
    nodes.set(SINK, {
      id: SINK, path: g.tex.path, sink: true, title: "output", sub: TGM.leaf(g.tex.path),
      inputs: [{ name: "output", type: "TexVector" }], h: GHEAD + thumb + GROW + 6, thumb, ins: [], outs: [], order: "~"
    });
  }
  const edges = g.edges.map((e) => Object.assign({}, e));
  if (g.output && nodes.has(g.output)) edges.push({ from: g.output, to: SINK, port: "output", index: 0, type: "TexVector" });
  for (const e of edges) {
    nodes.get(e.from).outs.push(e);
    nodes.get(e.to).ins.push(e);
  }
  return { g, nodes, edges, dangling: g.dangling };
};

G.layout = (m) => {
  const list = [...m.nodes.values()];
  const rank = new Map();
  const visiting = new Set();
  const rankOf = (n) => {
    if (rank.has(n.id)) return rank.get(n.id);
    if (visiting.has(n.id)) return 0;
    visiting.add(n.id);
    let v = 0;
    for (const e of n.outs) v = Math.max(v, 1 + rankOf(m.nodes.get(e.to)));
    visiting.delete(n.id);
    rank.set(n.id, v);
    return v;
  };
  list.forEach(rankOf);
  const maxR = Math.max(0, ...rank.values());
  const cols = [];
  const loose = [];
  for (const n of list) {
    if (!n.sink && !n.outs.length && !n.ins.length) {
      loose.push(n);
      continue;
    }
    n.col = maxR - rank.get(n.id);
    (cols[n.col] = cols[n.col] || []).push(n);
  }
  for (const col of cols) if (col) col.sort((a, b) => a.order < b.order ? -1 : 1);
  for (const col of cols) {
    if (!col) continue;
    let y = 0;
    for (const n of col) { n.y = y; y += n.h + GGAP; }
  }
  const center = (n) => n.y + n.h / 2;
  const bary = (n) => {
    const nb = n.ins.map((e) => m.nodes.get(e.from)).concat(n.outs.map((e) => m.nodes.get(e.to)))
      .filter((o) => o.col !== n.col && o.col !== undefined);
    return nb.length ? nb.reduce((s, o) => s + center(o), 0) / nb.length : null;
  };
  const settle = (col) => {
    for (const n of col) { const b = bary(n); n.want = b === null ? center(n) : b; }
    col.sort((a, b) => a.want - b.want);
    let y = -Infinity;
    for (const n of col) {
      n.y = Math.max(y, n.want - n.h / 2);
      y = n.y + n.h + GGAP;
    }
  };
  for (let it = 0; it < 10; it++) {
    const order = it % 2 ? cols.slice().reverse() : cols;
    for (const col of order) if (col) settle(col);
  }
  const placed = list.filter((n) => loose.indexOf(n) < 0);
  const minY = placed.length ? Math.min(...placed.map((n) => n.y)) : 0;
  let h = GPAD;
  for (const n of placed) {
    n.y = n.y - minY + GPAD;
    n.x = GPAD + n.col * GCOLW;
    h = Math.max(h, n.y + n.h + GPAD);
  }
  let x = GPAD;
  const y = h + 30;
  G.looseY = loose.length ? h + 6 : null;
  for (const n of loose) {
    n.x = x;
    n.y = y;
    x += GNODEW + GGAP;
  }
};

G.posKey = () => "texgraph.pos:" + (TG.host || "") + ":" + (TG.tex || "");

G.loadPos = () => JSON.parse(localStorage.getItem(G.posKey()) || "{}");

G.savePos = (pos) => {
  if (Object.keys(pos).length) localStorage.setItem(G.posKey(), JSON.stringify(pos));
  else localStorage.removeItem(G.posKey());
};

G.portXY = (n, side, index) => side === "out"
  ? { x: n.x + GNODEW, y: n.y + GHEAD / 2 }
  : { x: n.x, y: n.y + GHEAD + n.thumb + index * GROW + GROW / 2 };

G.curve = (a, b) => {
  const dx = b.x > a.x ? Math.max(40, (b.x - a.x) / 2) : 90;
  return `M${a.x},${a.y} C${a.x + dx},${a.y} ${b.x - dx},${b.y} ${b.x},${b.y}`;
};

G.drawEdges = () => {
  for (const p of G.edgeEls) {
    const a = G.cur.nodes.get(p.e.from), b = G.cur.nodes.get(p.e.to);
    p.el.setAttribute("d", G.curve(G.portXY(a, "out"), G.portXY(b, "in", p.e.index)));
  }
};

G.bounds = () => {
  let w = 0, h = 0;
  for (const n of G.cur.nodes.values()) {
    w = Math.max(w, n.x + GNODEW + GPAD);
    h = Math.max(h, n.y + n.h + GPAD);
  }
  return { w: Math.max(w, 400), h: Math.max(h, 300) };
};

G.resize = () => {
  G.size = G.bounds();
  const canvas = document.querySelector(".graph-canvas");
  const svg = document.querySelector(".gedges");
  if (!canvas || !svg) return;
  canvas.style.width = G.size.w + "px";
  canvas.style.height = G.size.h + "px";
  svg.setAttribute("width", G.size.w);
  svg.setAttribute("height", G.size.h);
  G.setZoom(G.zoom);
};

G.autoArrange = () => {
  G.savePos({});
  G.renderAll();
};

G.setZoom = (z, keep) => {
  const scroller = document.querySelector(".graph");
  const old = G.zoom;
  G.zoom = Math.max(0.25, Math.min(2, z));
  localStorage.setItem("texgraph.zoom", G.zoom);
  const sizer = document.querySelector(".graph-sizer");
  const canvas = document.querySelector(".graph-canvas");
  if (!sizer || !canvas) return;
  sizer.style.width = G.size.w * G.zoom + "px";
  sizer.style.height = G.size.h * G.zoom + "px";
  canvas.style.transform = `scale(${G.zoom})`;
  if (keep && scroller) {
    const k = G.zoom / old;
    scroller.scrollLeft = (scroller.scrollLeft + keep.x) * k - keep.x;
    scroller.scrollTop = (scroller.scrollTop + keep.y) * k - keep.y;
  }
  G.schedulePreviews();
};

G.fit = () => {
  const scroller = document.querySelector(".graph");
  if (!scroller || !G.size || !G.cur.nodes.size) return;
  const list = [...G.cur.nodes.values()];
  const x0 = Math.min(...list.map((n) => n.x)) - GPAD, y0 = Math.min(...list.map((n) => n.y)) - GPAD;
  const x1 = Math.max(...list.map((n) => n.x + GNODEW)) + GPAD, y1 = Math.max(...list.map((n) => n.y + n.h)) + GPAD;
  G.setZoom(Math.min(1, (scroller.clientWidth - 10) / (x1 - x0), (scroller.clientHeight - 10) / (y1 - y0)));
  scroller.scrollLeft = x0 * G.zoom;
  scroller.scrollTop = y0 * G.zoom;
};

G.applyPicked = () => {
  for (const id of [...G.picked]) if (!G.cur.nodes.has(id)) G.picked.delete(id);
  document.querySelectorAll(".tnode").forEach((el) => el.classList.toggle("picked", G.picked.has(el.dataset.id)));
};

G.hot = (id) => {
  for (const p of G.edgeEls) {
    const on = id && (p.e.from === id || p.e.to === id);
    p.el.classList.toggle("hot", !!on);
    p.el.classList.toggle("dim", !!id && !on);
  }
};

G.drag = (n, el, ev) => {
  if (ev.button !== 0 || ev.target.closest(".port, input, select, button")) return;
  const sx = ev.clientX, sy = ev.clientY;
  const group = (G.picked.has(n.id) ? [...G.picked] : [n.id]).map((id) => G.cur.nodes.get(id))
    .map((g) => ({ n: g, el: document.querySelector(`.tnode[data-id="${CSS.escape(g.id)}"]`), x: g.x, y: g.y }))
    .filter((g) => g.el);
  const minDx = -Math.min(...group.map((g) => g.x)), minDy = -Math.min(...group.map((g) => g.y));
  let moving = false;
  const move = (e) => {
    const dx = (e.clientX - sx) / G.zoom, dy = (e.clientY - sy) / G.zoom;
    if (!moving) {
      if (Math.abs(dx) + Math.abs(dy) < 4) return;
      moving = true;
      TG.dragging++;
      el.setPointerCapture(e.pointerId);
      for (const g of group) g.el.classList.add("dragging");
    }
    const ox = Math.round(Math.max(minDx, dx)), oy = Math.round(Math.max(minDy, dy));
    for (const g of group) {
      g.n.x = g.x + ox;
      g.n.y = g.y + oy;
      g.el.style.left = g.n.x + "px";
      g.el.style.top = g.n.y + "px";
    }
    G.drawEdges();
    G.resize();
  };
  const up = () => {
    el.removeEventListener("pointermove", move);
    el.removeEventListener("pointerup", up);
    el.removeEventListener("pointercancel", up);
    if (!moving) return;
    for (const g of group) g.el.classList.remove("dragging");
    TG.dragging--;
    G.justDragged = true;
    setTimeout(() => { G.justDragged = false; }, 0);
    const pos = G.loadPos();
    for (const g of group) pos[g.n.id] = [g.n.x, g.n.y];
    G.savePos(pos);
  };
  el.addEventListener("pointermove", move);
  el.addEventListener("pointerup", up);
  el.addEventListener("pointercancel", up);
};

G.marquee = (scroller, canvas, ev) => {
  if (ev.button !== 0 || ev.target.closest(".tnode") || ev.target.closest(".gedge")) return;
  const sr = scroller.getBoundingClientRect();
  if (ev.clientX - sr.left >= scroller.clientWidth || ev.clientY - sr.top >= scroller.clientHeight) return;
  const toCanvas = (e) => {
    const r = canvas.getBoundingClientRect();
    return { x: (e.clientX - r.left) / G.zoom, y: (e.clientY - r.top) / G.zoom };
  };
  const a = toCanvas(ev);
  const base = ev.shiftKey || ev.metaKey || ev.ctrlKey ? new Set(G.picked) : new Set();
  let box = null;
  const move = (e) => {
    if (!box) {
      if (Math.abs(e.clientX - ev.clientX) + Math.abs(e.clientY - ev.clientY) < 4) return;
      box = G.el("div", { class: "gmarquee" });
      canvas.appendChild(box);
      scroller.setPointerCapture(e.pointerId);
      TG.dragging++;
    }
    const b = toCanvas(e);
    const x0 = Math.min(a.x, b.x), y0 = Math.min(a.y, b.y), x1 = Math.max(a.x, b.x), y1 = Math.max(a.y, b.y);
    Object.assign(box.style, { left: x0 + "px", top: y0 + "px", width: x1 - x0 + "px", height: y1 - y0 + "px" });
    G.picked = new Set(base);
    for (const n of G.cur.nodes.values()) {
      if (n.sink) continue;
      if (n.x < x1 && n.x + GNODEW > x0 && n.y < y1 && n.y + n.h > y0) G.picked.add(n.id);
    }
    G.applyPicked();
  };
  const up = () => {
    scroller.removeEventListener("pointermove", move);
    scroller.removeEventListener("pointerup", up);
    scroller.removeEventListener("pointercancel", up);
    if (!box) return;
    box.remove();
    TG.dragging--;
    G.justDragged = true;
    setTimeout(() => { G.justDragged = false; }, 0);
  };
  scroller.addEventListener("pointermove", move);
  scroller.addEventListener("pointerup", up);
  scroller.addEventListener("pointercancel", up);
};

// Drag a wire from a port. From an output it connects to the input it is dropped on; from a
// connected input it takes the wire off that input, so dropping it elsewhere moves it and
// dropping it on nothing disconnects; from a free input it connects to the output it is
// dropped on. Ports whose type does not match are marked and refuse the wire.
G.wire = (ev, port) => {
  if (ev.button !== 0) return;
  ev.stopPropagation();
  ev.preventDefault();
  const canvas = document.querySelector(".graph-canvas");
  const svg = document.querySelector(".gedges");
  const dir = port.dataset.dir;
  const node = G.cur.nodes.get(port.dataset.id);
  let from = null, detach = null;
  if (dir === "out") {
    from = node.id;
  } else {
    const e = node.ins.find((x) => x.port === port.dataset.port);
    if (e) {
      from = e.from;
      detach = { to: node.id, port: e.port };
    }
  }
  const anchor = from ? G.portXY(G.cur.nodes.get(from), "out") : G.portXY(node, "in", +port.dataset.index);
  const path = document.createElementNS("http://www.w3.org/2000/svg", "path");
  path.setAttribute("class", "gedge wire " + G.typeClass(from ? (TG.kindOfNode(from) || {}).output : port.dataset.type));
  svg.appendChild(path);
  if (detach) {
    const old = G.edgeEls.find((p) => p.e.to === detach.to && p.e.port === detach.port);
    if (old) old.el.classList.add("hidden");
  }
  const targetDir = from ? "in" : "out";
  const check = (el) => {
    if (from) return TGM.connectError(TG, from, el.dataset.id === SINK ? SINK : el.dataset.id, el.dataset.port);
    return TGM.connectError(TG, el.dataset.id, node.id === SINK ? SINK : node.id, port.dataset.port);
  };
  document.querySelectorAll(`.port.${targetDir}`).forEach((el) => {
    const err = check(el);
    el.classList.add(err ? "refuse" : "accept");
  });
  TG.dragging++;
  const toCanvas = (e) => {
    const r = canvas.getBoundingClientRect();
    return { x: (e.clientX - r.left) / G.zoom, y: (e.clientY - r.top) / G.zoom };
  };
  const move = (e) => {
    const p = toCanvas(e);
    path.setAttribute("d", from ? G.curve(anchor, p) : G.curve(p, anchor));
  };
  const up = (e) => {
    document.removeEventListener("pointermove", move);
    document.removeEventListener("pointerup", up);
    document.removeEventListener("pointercancel", up);
    TG.dragging--;
    path.remove();
    document.querySelectorAll(".port.accept, .port.refuse").forEach((el) => el.classList.remove("accept", "refuse"));
    const hit = document.elementFromPoint(e.clientX, e.clientY);
    const target = hit ? hit.closest(`.port.${targetDir}`) : null;
    if (target && from) {
      if (detach && target.dataset.id === detach.to && target.dataset.port === detach.port) {
        G.renderAll();
        return;
      }
      if (detach && !check(target)) TG.disconnectPort(detach.to, detach.port);
      if (!TG.connectPorts(from, target.dataset.id, target.dataset.port)) G.renderAll();
    } else if (target && !from) {
      TG.connectPorts(target.dataset.id, node.id, port.dataset.port);
    } else if (detach) {
      TG.disconnectPort(detach.to, detach.port);
    }
  };
  document.addEventListener("pointermove", move);
  document.addEventListener("pointerup", up);
  document.addEventListener("pointercancel", up);
  move(ev);
};

G.addMenu = (ev) => {
  const kinds = Object.values(TG.kinds).sort((a, b) => a.id - b.id);
  if (!TG.tex) return;
  const items = [{ title: "add node" }];
  for (const k of kinds) {
    const ins = k.inputs.map((p) => p.name + (p.type === "TexScalar" ? ":s" : "")).join(", ");
    items.push({
      label: `${TGM.short(k.comp)}  ${ins ? "(" + ins + ")" : ""}`,
      doc: `${k.comp}: outputs ${TGM.typeLabel(k.output)}${k.neighbourhood ? ", reads neighbours" : ""}${k.animated ? ", animated" : ""}`,
      action: () => G.addNode(k.comp, ev)
    });
  }
  G.menu(ev.clientX, ev.clientY, items);
};

G.addNode = async (comp, ev) => {
  const path = await TG.createNode(comp);
  if (!path) return;
  const canvas = document.querySelector(".graph-canvas");
  const scroller = document.querySelector(".graph");
  let x = GPAD, y = GPAD;
  if (canvas && ev && ev.clientX !== undefined && ev.target && ev.target.closest && ev.target.closest(".graph")) {
    const r = canvas.getBoundingClientRect();
    x = (ev.clientX - r.left) / G.zoom;
    y = (ev.clientY - r.top) / G.zoom;
  } else if (scroller) {
    x = scroller.scrollLeft / G.zoom + 40;
    y = scroller.scrollTop / G.zoom + 40;
  }
  const pos = G.loadPos();
  pos[path] = [Math.round(Math.max(0, x)), Math.round(Math.max(0, y))];
  G.savePos(pos);
  TG.select(path);
  G.renderAll();
};

G.nodeMenu = (ev, n) => {
  ev.preventDefault();
  ev.stopPropagation();
  if (n.sink) return;
  const items = [
    { label: "set as graph output", action: () => TG.connectPorts(n.id, SINK, "output") },
    { label: "select", action: () => TG.select(n.path) },
    null,
    { label: G.picked.size > 1 && G.picked.has(n.id) ? `delete ${G.picked.size} nodes` : "delete", action: () => G.deleteNodes(G.picked.has(n.id) ? [...G.picked] : [n.id]) }
  ];
  G.menu(ev.clientX, ev.clientY, items);
};

G.deleteNodes = (paths) => {
  const list = paths.filter((p) => p !== SINK && TG.nodes[p]);
  if (!list.length) return;
  G.picked.clear();
  TG.deleteNodes(list);
};

G.deleteSelection = () => {
  if (G.selEdge) {
    TG.disconnectPort(G.selEdge.to, G.selEdge.port);
    G.selEdge = null;
  } else if (G.picked.size) {
    G.deleteNodes([...G.picked]);
  } else if (TG.sel && TG.nodes[TG.sel]) {
    G.deleteNodes([TG.sel]);
  }
};

// Which nodes want a preview: the ones in the visible part of the canvas at the thumbnail
// size, and the selected one at the size of the large preview.
G.updatePreviews = () => {
  G.previewTimer = null;
  const wanted = new Map();
  const scroller = document.querySelector(".graph");
  if (scroller && G.cur) {
    const x0 = scroller.scrollLeft / G.zoom, y0 = scroller.scrollTop / G.zoom;
    const x1 = x0 + scroller.clientWidth / G.zoom, y1 = y0 + scroller.clientHeight / G.zoom;
    for (const n of G.cur.nodes.values()) {
      if (n.sink || !n.kind) continue;
      if (n.x < x1 && n.x + GNODEW > x0 && n.y < y1 && n.y + n.h > y0) wanted.set(n.path, 128);
    }
  }
  if (TG.sel && TG.nodes[TG.sel] && TG.kindOfNode(TG.sel)) wanted.set(TG.sel, 512);
  TG.syncPreviews(wanted);
};

G.schedulePreviews = () => {
  if (!G.previewTimer) G.previewTimer = setTimeout(G.updatePreviews, 150);
};

G.showPreviews = (paths) => {
  for (const path of paths) {
    const p = TG.previews[path];
    document.querySelectorAll(`img[data-preview="${CSS.escape(path)}"]`).forEach((img) => {
      if (p && p.src) {
        img.src = p.src;
        img.classList.remove("empty");
      } else {
        img.removeAttribute("src");
        img.classList.add("empty");
      }
    });
  }
  if (G.cur && G.cur.g.output && paths.includes(G.cur.g.output)) G.showPreviews([SINK]);
};

G.previewImg = (path) => {
  const src = path === SINK ? (G.cur && G.cur.g.output) : path;
  const p = src ? TG.previews[src] : null;
  return G.el("img", { "data-preview": path, class: p && p.src ? "" : "empty", src: p && p.src ? p.src : undefined, draggable: "false" });
};

G.inputSummary = (n, p) => {
  if (n.sink || !p.value) return "";
  const v = (TGM.nodeValue(n.rec) || {})[p.value];
  if (v === undefined || v === null) return "";
  if (typeof v === "number") return TGM.num(+v.toFixed(3));
  if (typeof v === "object") return Object.values(v).map((x) => TGM.num(+(+x).toFixed(2))).join(" ");
  return String(v);
};

G.renderNode = (canvas, n, errors) => {
  const sel = (n.sink && !TG.sel) || (!n.sink && TG.sel === n.path);
  const err = !n.sink && errors.indexOf(n.path) >= 0;
  const el = G.el("div", {
    class: "tnode" + (n.sink ? " sink" : "") + (sel ? " sel" : "") + (err ? " err" : "") + (n.kind ? "" : " ghost"),
    "data-id": n.id,
    title: n.sink ? `output of ${n.path}` : n.path + (err ? "\n" + TG.textures[TG.tex].status.error : ""),
    style: { left: n.x + "px", top: n.y + "px", width: GNODEW + "px", height: n.h + "px" },
    onclick: (ev) => {
      if (G.justDragged || ev.target.closest(".port")) return;
      G.selEdge = null;
      if (!n.sink && (ev.shiftKey || ev.metaKey || ev.ctrlKey)) {
        if (G.picked.has(n.id)) G.picked.delete(n.id);
        else G.picked.add(n.id);
        G.applyPicked();
        return;
      }
      if (G.picked.size) {
        G.picked.clear();
        G.applyPicked();
      }
      TG.select(n.sink ? null : n.path);
    },
    oncontextmenu: (ev) => G.nodeMenu(ev, n),
    onmouseenter: () => G.hot(n.id),
    onmouseleave: () => G.hot(null)
  });
  const out = n.sink ? null : G.el("span", {
    class: "port out " + G.typeClass(n.kind ? n.kind.output : ""), "data-dir": "out", "data-id": n.id,
    title: `output: ${n.kind ? TGM.typeLabel(n.kind.output) : "none"}, drag to an input`
  });
  if (out) out.addEventListener("pointerdown", (ev) => G.wire(ev, out));
  el.appendChild(G.el("div", { class: "thead" },
    G.el("span", { class: "swatch", style: { background: n.sink ? "var(--accent)" : G.color(n.sub) } }),
    G.el("div", { class: "ttext" }, G.el("div", { class: "ttitle" }, n.title), G.el("div", { class: "tsub" }, n.sub)),
    out));
  el.appendChild(G.el("div", { class: "tthumb", style: { height: n.thumb + "px" } }, G.previewImg(n.sink ? SINK : n.path)));
  n.inputs.forEach((p, i) => {
    const edge = n.ins.find((e) => e.port === p.name);
    const dangling = (G.cur.dangling || []).find((d) => d.to === n.id && d.port === p.name);
    const port = G.el("span", {
      class: "port in " + G.typeClass(p.type) + (edge ? " on" : "") + (dangling ? " missing" : ""),
      "data-dir": "in", "data-id": n.id, "data-port": p.name, "data-index": i, "data-type": p.type,
      title: `${p.name}: takes ${TGM.typeLabel(p.type)}` + (dangling ? `\nmissing node ${dangling.ref}` : edge ? `\nfrom ${edge.from}, drag to move or disconnect` : "")
    });
    port.addEventListener("pointerdown", (ev) => G.wire(ev, port));
    el.appendChild(G.el("div", { class: "trow" }, port,
      G.el("span", { class: "pname" }, p.name),
      G.el("span", { class: "pval" }, edge ? TGM.leaf(edge.from) : dangling ? "missing" : G.inputSummary(n, p))));
  });
  el.addEventListener("pointerdown", (ev) => G.drag(n, el, ev));
  canvas.appendChild(el);
};

G.render = (root) => {
  const m = G.model();
  G.cur = m;
  G.layout(m);
  const pos = G.loadPos();
  for (const n of m.nodes.values()) {
    const p = pos[n.id];
    if (!p) continue;
    n.x = p[0];
    n.y = p[1];
  }
  G.size = G.bounds();
  G.edgeEls = [];
  const tex = m.g.tex;
  const status = tex ? tex.status : null;
  const errors = status && status.error ? TGM.errorNodes(status.error) : [];
  const banner = status && status.error
    ? G.el("div", { class: "gerror", title: status.error }, "⚠ " + status.error)
    : null;
  const tools = G.el("div", { class: "graph-tools" },
    G.el("span", { class: "glegend" }, G.el("i", { class: "t-vector" }), "vector"),
    G.el("span", { class: "glegend" }, G.el("i", { class: "t-scalar" }), "scalar"),
    G.el("button", { class: "small", text: "+ node", title: "Add a node of a kind (or right click the canvas)", onclick: (ev) => G.addMenu(ev) }),
    G.el("span", { class: "grow" }),
    G.el("button", { class: "small", text: "auto arrange", title: "Discard moved nodes and restore the default layout", onclick: () => G.autoArrange() }),
    G.el("button", { class: "small", text: "−", onclick: () => G.setZoom(G.zoom / 1.2) }),
    G.el("button", { class: "small", text: "+", onclick: () => G.setZoom(G.zoom * 1.2) }),
    G.el("button", { class: "small", text: "fit", onclick: () => G.fit() }));
  const canvas = G.el("div", { class: "graph-canvas", style: { width: G.size.w + "px", height: G.size.h + "px" } });
  const sizer = G.el("div", { class: "graph-sizer" }, canvas);
  const scroller = G.el("div", { class: "graph" }, sizer);
  root.appendChild(G.el("div", { class: "graph-wrap" }, tools, banner, scroller));

  if (!tex) {
    canvas.appendChild(G.el("div", { class: "gempty" }, TG.connected ? "no TextureGraph in this app" : "not connected"));
  }

  const svgNs = "http://www.w3.org/2000/svg";
  const svg = document.createElementNS(svgNs, "svg");
  svg.setAttribute("width", G.size.w);
  svg.setAttribute("height", G.size.h);
  svg.classList.add("gedges");
  canvas.appendChild(svg);
  if (G.looseY !== null) canvas.appendChild(G.el("div", { class: "gcol", style: { left: GPAD + "px", top: G.looseY + "px" } }, "not connected"));

  for (const e of m.edges) {
    const p = document.createElementNS(svgNs, "path");
    const sel = G.selEdge && G.selEdge.to === e.to && G.selEdge.port === e.port;
    p.setAttribute("class", "gedge " + G.typeClass(e.type) + (sel ? " sel" : ""));
    const tip = document.createElementNS(svgNs, "title");
    tip.textContent = `${TGM.leaf(e.from)} → ${e.to === SINK ? "graph output" : TGM.leaf(e.to) + "." + e.port}\nclick to select, Delete or right click to disconnect`;
    p.appendChild(tip);
    p.addEventListener("click", (ev) => {
      ev.stopPropagation();
      G.selEdge = { to: e.to, port: e.port };
      G.edgeEls.forEach((x) => x.el.classList.toggle("sel", x.e === e));
    });
    p.addEventListener("contextmenu", (ev) => {
      ev.preventDefault();
      ev.stopPropagation();
      TG.disconnectPort(e.to, e.port);
    });
    svg.appendChild(p);
    G.edgeEls.push({ e, el: p });
  }
  G.drawEdges();

  for (const n of m.nodes.values()) G.renderNode(canvas, n, errors);

  scroller.addEventListener("pointerdown", (ev) => G.marquee(scroller, canvas, ev));
  scroller.addEventListener("click", (ev) => {
    if (ev.target.closest(".tnode") || ev.target.closest(".gedge") || G.justDragged) return;
    G.selEdge = null;
    G.edgeEls.forEach((x) => x.el.classList.remove("sel"));
    if (G.picked.size) {
      G.picked.clear();
      G.applyPicked();
    }
    if (TG.sel) TG.select(null);
  });
  scroller.addEventListener("contextmenu", (ev) => {
    if (ev.target.closest(".tnode") || ev.target.closest(".gedge")) return;
    ev.preventDefault();
    G.addMenu(ev);
  });
  scroller.addEventListener("wheel", (ev) => {
    if (!ev.ctrlKey && !ev.metaKey) return;
    ev.preventDefault();
    const r = scroller.getBoundingClientRect();
    G.setZoom(G.zoom * Math.exp(-ev.deltaY * 0.01), { x: ev.clientX - r.left, y: ev.clientY - r.top });
  }, { passive: false });
  scroller.addEventListener("scroll", () => G.schedulePreviews());
  if (G.scroll && G.scroll.tex === TG.tex) {
    scroller.scrollLeft = G.scroll.x;
    scroller.scrollTop = G.scroll.y;
  }
  scroller.addEventListener("scroll", () => { G.scroll = { tex: TG.tex, x: scroller.scrollLeft, y: scroller.scrollTop }; });
  G.setZoom(G.zoom);
  G.applyPicked();
  G.schedulePreviews();
};

G.renderAll = () => {
  const root = document.getElementById("view");
  const scroller = document.querySelector(".graph");
  if (scroller) G.scroll = { tex: TG.tex, x: scroller.scrollLeft, y: scroller.scrollTop };
  root.textContent = "";
  G.render(root);
  TG.needRender = false;
};

// Update the values of the free inputs shown on the nodes without rendering the graph again.
G.refreshValues = () => {
  if (!G.cur) return;
  document.querySelectorAll(".tnode .port.in").forEach((port) => {
    const id = port.dataset.id;
    if (id === SINK || port.classList.contains("on") || port.classList.contains("missing")) return;
    const n = G.cur.nodes.get(id);
    const rec = TG.nodes[id];
    if (!n || !rec) return;
    const p = n.inputs[+port.dataset.index];
    const val = port.parentNode.querySelector(".pval");
    if (p && val) val.textContent = G.inputSummary({ rec }, p);
  });
};

G.markSelected = () => {
  document.querySelectorAll(".tnode").forEach((e) => {
    const sink = e.dataset.id === SINK;
    e.classList.toggle("sel", sink ? !TG.sel : TG.sel === e.dataset.id);
  });
};
