"use strict";

const G = window.G = {
  zoom: parseFloat(localStorage.getItem("daw.graph.zoom") || "1"),
  edgeEls: [],
  picked: new Set()
};

const GPAD = 24, GCOLW = 260, GNODEW = 184, GGAP = 16, GHEAD = 36, GCHIP = 18;

const KIND_RANK = { sound: 0, clip: 1, instrument: 2, source: 3, modulator: 4, macro: 5, track: 6, bus: 7, effect: 8, master: 9 };

const EDGE_TYPES = [
  { type: "audio", label: "audio" },
  { type: "send", label: "send" },
  { type: "notes", label: "notes" },
  { type: "sound", label: "sound" },
  { type: "mod", label: "modulation" },
  { type: "key", label: "sidechain" }
];

G.model = () => {
  const nodes = new Map();
  const edges = [];
  const edgeKeys = new Map();
  const fxOwner = {};
  const master = A + "master";

  const add = (id, n) => {
    if (!nodes.has(id)) nodes.set(id, Object.assign({ id, path: id, ins: [], outs: [], inserts: [] }, n));
    return nodes.get(id);
  };

  const resolve = (path) => {
    if (DAW.isNone(path)) return null;
    if (nodes.has(path)) return { id: path };
    if (fxOwner[path]) return { id: fxOwner[path].bus, port: fxOwner[path].slot };
    let p = path;
    while (p && p.includes(".")) {
      p = p.slice(0, p.lastIndexOf("."));
      if (nodes.has(p)) return { id: p };
    }
    return null;
  };

  const link = (from, to, type, label) => {
    if (!from || !to || from.id === to.id) return;
    const key = `${from.id}|${to.id}|${type}|${to.port === undefined ? "" : to.port}`;
    const prev = edgeKeys.get(key);
    if (prev) {
      prev.count++;
      if (label && prev.labels.indexOf(label) < 0) prev.labels.push(label);
      return;
    }
    const e = { from: from.id, to: to.id, toPort: to.port, type, count: 1, labels: label ? [label] : [] };
    edgeKeys.set(key, e);
    edges.push(e);
  };

  add(master, { kind: "master", title: "Master", sub: "output", rec: DAW.ents[master], order: 1e9, meter: true });

  for (const b of DAW.buses()) {
    if (b.path === master) continue;
    const isTrack = DAW.has(b, "Track");
    const bus = b.comps[A + "Bus"];
    add(b.path, {
      kind: isTrack ? "track" : "bus", rec: b, order: b.order, meter: true,
      title: isTrack ? V.laneName(b.path) : b.name,
      sub: isTrack ? "track" : UI.enumLabel(bus.kind).toLowerCase()
    });
  }
  const masterRec = DAW.ents[master];
  if (masterRec) nodes.get(master).rec = masterRec;

  for (const n of nodes.values()) {
    if (!n.rec || !DAW.has(n.rec, "Bus")) continue;
    (n.rec.comps[A + "Bus"].effects || []).forEach((fx, slot) => {
      if (DAW.isNone(fx)) return;
      const frec = DAW.ents[fx];
      const label = frec ? DAW.effectComps(frec).map(DAW.short).join(" / ") || DAW.leaf(fx) : DAW.leaf(fx);
      n.inserts.push({ path: fx, slot, label, off: frec && frec.disabled });
      fxOwner[fx] = { bus: n.id, slot: n.inserts.length - 1 };
    });
  }

  for (const r of DAW.effects()) {
    if (fxOwner[r.path] || nodes.has(r.path)) continue;
    add(r.path, { kind: "effect", rec: r, order: r.order, title: r.name, sub: DAW.effectComps(r).map(DAW.short).join(" / ") + " (no slot)" });
  }

  for (const r of DAW.list("Instrument")) {
    const inst = r.comps[A + "Instrument"];
    add(r.path, { kind: "instrument", rec: r, order: r.order, title: r.name, sub: "instrument" + (inst.polyphony ? ` ${inst.polyphony} voices` : "") });
  }

  for (const r of DAW.list("Modulator")) {
    const mv = r.comps[A + "Modulator"];
    add(r.path, { kind: "modulator", rec: r, order: r.order, title: r.name, sub: `modulator ${UI.enumLabel(mv.shape || "").toLowerCase()}`.trim() });
  }

  for (const r of DAW.list("Macro")) {
    add(r.path, { kind: "macro", rec: r, order: r.order, title: r.name, sub: `macro ${(r.comps[A + "Macro"].value || 0).toFixed(2)}` });
  }

  const usedClips = new Set();
  for (const r of Object.values(DAW.ents)) {
    if (DAW.has(r, "ClipPlacement")) usedClips.add(r.comps[A + "ClipPlacement"].clip);
  }
  for (const r of DAW.clips()) {
    const kind = DAW.has(r, "NoteClip") ? "notes" : DAW.has(r, "AutomationClip") ? "automation" : "audio";
    const sub = kind === "notes" ? `${(r.comps[A + "NoteClip"].notes || []).length} notes` : `${kind} clip`;
    add(r.path, { kind: "clip", rec: r, order: r.order, title: r.name, sub: sub + (usedClips.has(r.path) ? "" : ", unplaced") });
  }

  const soundRefs = new Set();
  for (const r of DAW.list("Instrument")) soundRefs.add(r.comps[A + "Instrument"].sound);
  for (const r of DAW.list("Clip")) soundRefs.add(r.comps[A + "Clip"].sound);
  for (const r of Object.values(DAW.ents)) {
    if (DAW.has(r, "Source")) soundRefs.add(r.comps[A + "Source"].sound);
  }
  for (const r of DAW.sounds()) {
    if (nodes.has(r.path)) continue;
    const isSynth = DAW.has(r, "SynthSound");
    if (!isSynth && !soundRefs.has(r.path)) continue;
    add(r.path, { kind: "sound", rec: r, order: r.order, title: r.name, sub: isSynth ? "synth" : "sample" });
  }

  const sources = Object.values(DAW.ents).filter((r) =>
    (DAW.has(r, "Source") || DAW.has(r, "Stream")) && !nodes.has(r.path))
    .sort((a, b) => a.order - b.order);
  const groups = new Map();
  for (const r of sources) {
    const out = r.comps[A + "Output"];
    const bus = out && !DAW.isNone(out.bus) ? out.bus : master;
    const anon = r.name.startsWith("#");
    const id = anon ? `sources:${r.parent || ""}:${bus}` : r.path;
    let g = groups.get(id);
    if (!g) {
      g = { id, bus, members: [], anon };
      groups.set(id, g);
    }
    g.members.push(r);
  }
  for (const g of groups.values()) {
    const first = g.members[0];
    const stream = DAW.has(first, "Stream");
    const n = add(g.id, {
      kind: "source", rec: first, path: first.path, order: first.order,
      title: g.anon ? `${g.members.length} ${stream ? "streams" : "sources"}` : first.name,
      sub: g.anon ? `in ${DAW.leaf(first.parent) || "root"}` : (stream ? "stream" : "source") + (first.parent ? ` in ${DAW.leaf(first.parent)}` : "")
    });
    n.members = g.members.map((m) => m.path);
    const to = resolve(g.bus) || { id: master };
    link({ id: g.id }, to, "audio");
    for (const m of g.members) {
      const s = m.comps[A + "Source"];
      if (s) link(resolve(s.sound), { id: g.id }, "sound");
    }
  }

  for (const n of [...nodes.values()]) {
    if (!n.rec || !DAW.has(n.rec, "Bus") || n.id === master) continue;
    const bus = n.rec.comps[A + "Bus"];
    link({ id: n.id }, resolve(bus.output) || { id: master }, "audio");
    for (const s of bus.sends || []) {
      link({ id: n.id }, resolve(s.bus), "send", `${s.pre_fader ? "pre " : ""}${(+s.level).toFixed(2)}`);
    }
  }

  for (const t of DAW.tracks()) {
    const tr = t.comps[A + "Track"];
    if (!DAW.isNone(tr.instrument)) link(resolve(tr.instrument), { id: t.path }, "notes");
  }

  for (const r of DAW.list("Instrument")) {
    link(resolve(r.comps[A + "Instrument"].sound), { id: r.path }, "sound");
  }

  for (const r of DAW.list("Clip")) {
    link(resolve(r.comps[A + "Clip"].sound), { id: r.path }, "sound");
  }

  for (const r of Object.values(DAW.ents)) {
    if (!DAW.has(r, "ClipPlacement")) continue;
    const cp = r.comps[A + "ClipPlacement"];
    const clip = DAW.ents[cp.clip];
    if (!clip) continue;
    if (DAW.has(clip, "AutomationClip")) continue;
    link(resolve(cp.clip), resolve(r.parent), DAW.has(clip, "Clip") ? "audio" : "notes");
  }

  for (const r of DAW.list("AutomationClip")) {
    const ac = r.comps[A + "AutomationClip"];
    link({ id: r.path }, resolve(ac.target), "mod", ac.member);
  }

  for (const r of DAW.list("Modulator")) {
    const mv = r.comps[A + "Modulator"];
    link({ id: r.path }, resolve(mv.target), "mod", mv.member);
    link(resolve(mv.key), { id: r.path }, "key");
  }

  for (const r of DAW.list("Macro")) {
    for (const c of DAW.childrenOf(r.path)) {
      const mm = c.comps[A + "MacroMap"];
      if (mm) link({ id: r.path }, resolve(mm.target), "mod", mm.member);
    }
  }

  for (const r of DAW.effects()) {
    const here = resolve(r.path);
    for (const comp of DAW.effectComps(r)) {
      const v = r.comps[comp] || {};
      if (v.sidechain !== undefined) link(resolve(v.sidechain), here, "key");
      if (v.modulator !== undefined && DAW.short(comp) === "Vocoder") link(resolve(v.modulator), here, "key");
    }
  }

  for (const e of edges) {
    nodes.get(e.from).outs.push(e);
    nodes.get(e.to).ins.push(e);
  }
  return { nodes, edges };
};

G.layout = (m) => {
  const list = [...m.nodes.values()];
  for (const n of list) n.h = GHEAD + n.inserts.length * GCHIP + (n.inserts.length ? 6 : 0);
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
    if (!n.ins.length && !n.outs.length && n.kind !== "master") {
      n.col = 0;
      loose.push(n);
      continue;
    }
    n.col = maxR - rank.get(n.id);
    (cols[n.col] = cols[n.col] || []).push(n);
  }
  for (const col of cols) {
    if (col) col.sort((a, b) => KIND_RANK[a.kind] - KIND_RANK[b.kind] || a.order - b.order);
  }
  const stack = (col) => {
    let y = 0;
    for (const n of col) { n.y = y; y += n.h + GGAP; }
  };
  cols.forEach((c) => c && stack(c));
  const center = (n) => n.y + n.h / 2;
  const bary = (n) => {
    const nb = n.ins.map((e) => m.nodes.get(e.from)).concat(n.outs.map((e) => m.nodes.get(e.to)))
      .filter((o) => o.col !== n.col);
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
  const minY = Math.min(...placed.map((n) => n.y));
  let w = 0, h = GPAD + 30;
  for (const n of placed) {
    n.y = n.y - minY + GPAD + 30;
    n.x = GPAD + n.col * GCOLW;
    w = Math.max(w, n.x + GNODEW + GPAD);
    h = Math.max(h, n.y + n.h + GPAD);
  }
  loose.sort((a, b) => KIND_RANK[a.kind] - KIND_RANK[b.kind] || a.order - b.order);
  G.looseY = loose.length ? h + 20 : null;
  let y = h + 46;
  for (const n of loose) {
    n.x = GPAD;
    n.y = y;
    y += n.h + GGAP;
    w = Math.max(w, n.x + GNODEW + GPAD);
    h = Math.max(h, n.y + n.h + GPAD);
  }
  return { w, h };
};

G.portY = (n, e, side) => {
  if (side === "in" && e.toPort !== undefined) return n.y + GHEAD + e.toPort * GCHIP + GCHIP / 2;
  const list = side === "in" ? n.ins.filter((x) => x.toPort === undefined) : n.outs;
  const other = (x) => {
    const o = G.cur.nodes.get(side === "in" ? x.from : x.to);
    return o.y + o.h / 2;
  };
  const sorted = list.slice().sort((a, b) => other(a) - other(b));
  const i = sorted.indexOf(e);
  const span = Math.min(GHEAD - 10, n.h - 10);
  return n.y + (n.h - span) / 2 + span * (i + 1) / (sorted.length + 1);
};

G.hot = (id) => {
  for (const p of G.edgeEls) {
    const on = id && (p.e.from === id || p.e.to === id);
    p.el.classList.toggle("hot", !!on);
    p.el.classList.toggle("dim", !!id && !on);
  }
  document.querySelectorAll(".gnode").forEach((el) => {
    const linked = id && (el.dataset.id === id || G.edgeEls.some((p) =>
      (p.e.from === id && p.e.to === el.dataset.id) || (p.e.to === id && p.e.from === el.dataset.id)));
    el.classList.toggle("dim", !!id && !linked);
  });
};

G.posKey = () => {
  const song = DAW.single.SongFile;
  return "daw.graph.pos:" + (song && song.file ? song.file : DAW.host || "");
};

G.loadPos = () => JSON.parse(localStorage.getItem(G.posKey()) || "{}");

G.savePos = (pos) => {
  if (Object.keys(pos).length) localStorage.setItem(G.posKey(), JSON.stringify(pos));
  else localStorage.removeItem(G.posKey());
};

G.reachable = (id) => {
  const m = G.cur;
  const seen = new Set([id]);
  const walk = (start, next) => {
    const stack = [start];
    const local = new Set([start]);
    while (stack.length) {
      const n = m.nodes.get(stack.pop());
      for (const o of next(n)) {
        if (local.has(o)) continue;
        local.add(o);
        seen.add(o);
        stack.push(o);
      }
    }
  };
  walk(id, (n) => n.outs.map((e) => e.to));
  walk(id, (n) => n.ins.map((e) => e.from));
  return seen;
};

G.applyFocus = () => {
  if (!G.cur) return;
  if (G.focus && !G.cur.nodes.has(G.focus)) G.focus = null;
  const vis = G.focus ? G.reachable(G.focus) : null;
  if (vis) for (const id of [...G.picked]) if (!vis.has(id)) G.picked.delete(id);
  G.applyPicked();
  document.querySelectorAll(".gnode").forEach((el) => el.classList.toggle("hidden", !!vis && !vis.has(el.dataset.id)));
  for (const p of G.edgeEls) p.el.classList.toggle("hidden", !!vis && !(vis.has(p.e.from) && vis.has(p.e.to)));
  document.querySelectorAll(".gcol").forEach((el) => el.classList.toggle("hidden", !!vis || G.custom));
  if (G.focusEl) {
    G.focusEl.textContent = "";
    if (vis) {
      const n = G.cur.nodes.get(G.focus);
      G.focusEl.append(`${n.title}: ${vis.size} of ${G.cur.nodes.size} nodes`,
        UI.el("button", { class: "small", text: "show all", onclick: () => G.clearFocus() }));
    }
  }
};

G.applyPicked = () => {
  for (const id of [...G.picked]) if (!G.cur.nodes.has(id)) G.picked.delete(id);
  document.querySelectorAll(".gnode").forEach((el) => el.classList.toggle("picked", G.picked.has(el.dataset.id)));
};

G.togglePick = (n) => {
  if (G.picked.has(n.id)) G.picked.delete(n.id);
  else G.picked.add(n.id);
  G.applyPicked();
};

G.setFocus = (n) => {
  G.focus = n.id;
  G.applyFocus();
  DAW.select(n.rec ? n.path : null);
};

G.clearFocus = () => {
  G.focus = null;
  G.picked.clear();
  G.applyFocus();
  if (DAW.sel) DAW.select(null);
};

G.edgeShape = (e) => {
  const a = G.cur.nodes.get(e.from), b = G.cur.nodes.get(e.to);
  const x1 = a.x + GNODEW, y1 = G.portY(a, e, "out");
  const x2 = b.x, y2 = G.portY(b, e, "in");
  const dx = x2 > x1 ? Math.max(40, (x2 - x1) / 2) : 90;
  return { d: `M${x1},${y1} C${x1 + dx},${y1} ${x2 - dx},${y2} ${x2},${y2}`, mx: (x1 + x2) / 2, my: (y1 + y2) / 2 - 3 };
};

G.drawEdges = () => {
  for (const p of G.edgeEls) {
    const sh = G.edgeShape(p.e);
    if (p.label) {
      p.el.setAttribute("x", sh.mx);
      p.el.setAttribute("y", sh.my);
    } else {
      p.el.setAttribute("d", sh.d);
    }
  }
};

G.bounds = () => {
  let w = 0, h = 0;
  for (const n of G.cur.nodes.values()) {
    w = Math.max(w, n.x + GNODEW + GPAD);
    h = Math.max(h, n.y + n.h + GPAD);
  }
  return { w, h };
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
  V.renderAll();
};

G.drag = (n, el, ev) => {
  if (ev.button !== 0) return;
  const sx = ev.clientX, sy = ev.clientY;
  const group = (G.picked.has(n.id) ? [...G.picked] : [n.id]).map((id) => G.cur.nodes.get(id))
    .map((g) => ({ n: g, el: document.querySelector(`.gnode[data-id="${CSS.escape(g.id)}"]`), x: g.x, y: g.y }))
    .filter((g) => g.el);
  const minDx = -Math.min(...group.map((g) => g.x)), minDy = -Math.min(...group.map((g) => g.y));
  let moving = false;
  const move = (e) => {
    const dx = (e.clientX - sx) / G.zoom, dy = (e.clientY - sy) / G.zoom;
    if (!moving) {
      if (Math.abs(dx) + Math.abs(dy) < 4) return;
      moving = true;
      DAW.dragging++;
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
    DAW.dragging--;
    G.justDragged = true;
    setTimeout(() => { G.justDragged = false; }, 0);
    const pos = G.loadPos();
    for (const g of group) pos[g.n.id] = [g.n.x, g.n.y];
    G.savePos(pos);
    if (!G.custom) {
      G.custom = true;
      G.applyFocus();
    }
  };
  el.addEventListener("pointermove", move);
  el.addEventListener("pointerup", up);
  el.addEventListener("pointercancel", up);
};

G.setZoom = (z, keep) => {
  const scroller = document.querySelector(".graph");
  const old = G.zoom;
  G.zoom = Math.max(0.3, Math.min(2, z));
  localStorage.setItem("daw.graph.zoom", G.zoom);
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
};

G.fit = () => {
  const scroller = document.querySelector(".graph");
  if (!scroller || !G.size) return;
  const vis = G.focus ? G.reachable(G.focus) : null;
  const list = [...G.cur.nodes.values()].filter((n) => !vis || vis.has(n.id));
  if (!list.length) return;
  const x0 = Math.min(...list.map((n) => n.x)) - GPAD, y0 = Math.min(...list.map((n) => n.y)) - GPAD;
  const x1 = Math.max(...list.map((n) => n.x + GNODEW)) + GPAD, y1 = Math.max(...list.map((n) => n.y + n.h)) + GPAD;
  G.setZoom(Math.min(1, (scroller.clientWidth - 10) / (x1 - x0), (scroller.clientHeight - 10) / (y1 - y0)));
  scroller.scrollLeft = x0 * G.zoom;
  scroller.scrollTop = y0 * G.zoom;
};

G.marquee = (scroller, canvas, ev) => {
  if (ev.button !== 0 || ev.target.closest(".gnode")) return;
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
      box = UI.el("div", { class: "gmarquee" });
      canvas.appendChild(box);
      scroller.setPointerCapture(e.pointerId);
      DAW.dragging++;
    }
    const b = toCanvas(e);
    const x0 = Math.min(a.x, b.x), y0 = Math.min(a.y, b.y), x1 = Math.max(a.x, b.x), y1 = Math.max(a.y, b.y);
    Object.assign(box.style, { left: x0 + "px", top: y0 + "px", width: x1 - x0 + "px", height: y1 - y0 + "px" });
    const vis = G.focus ? G.reachable(G.focus) : null;
    G.picked = new Set(base);
    for (const n of G.cur.nodes.values()) {
      if (vis && !vis.has(n.id)) continue;
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
    DAW.dragging--;
    G.justDragged = true;
    setTimeout(() => { G.justDragged = false; }, 0);
  };
  scroller.addEventListener("pointermove", move);
  scroller.addEventListener("pointerup", up);
  scroller.addEventListener("pointercancel", up);
};

G.render = (root) => {
  const m = G.model();
  G.cur = m;
  G.layout(m);
  const pos = G.loadPos();
  G.custom = false;
  for (const n of m.nodes.values()) {
    const p = pos[n.id];
    if (!p) continue;
    n.x = p[0];
    n.y = p[1];
    G.custom = true;
  }
  G.size = G.bounds();
  G.edgeEls = [];
  const legend = EDGE_TYPES.map((t) => UI.el("span", { class: "glegend" },
    UI.el("i", { class: "e-" + t.type }), t.label));
  G.focusEl = UI.el("span", { class: "gfocus" });
  const tools = UI.el("div", { class: "arrange-tools" },
    ...legend,
    G.focusEl,
    UI.el("span", { class: "grow" }),
    UI.el("button", { class: "small", text: "auto arrange", title: "Discard moved nodes and restore the default layout", onclick: () => G.autoArrange() }),
    UI.el("button", { class: "small", text: "−", onclick: () => G.setZoom(G.zoom / 1.2) }),
    UI.el("button", { class: "small", text: "+", onclick: () => G.setZoom(G.zoom * 1.2) }),
    UI.el("button", { class: "small", text: "fit", onclick: () => G.fit() }));
  const canvas = UI.el("div", { class: "graph-canvas", style: { width: G.size.w + "px", height: G.size.h + "px" } });
  const sizer = UI.el("div", { class: "graph-sizer" }, canvas);
  const scroller = UI.el("div", { class: "graph" }, sizer);
  root.appendChild(UI.el("div", { class: "graph-wrap" }, tools, scroller));

  const svgNs = "http://www.w3.org/2000/svg";
  const svg = document.createElementNS(svgNs, "svg");
  svg.setAttribute("width", G.size.w);
  svg.setAttribute("height", G.size.h);
  svg.classList.add("gedges");
  canvas.appendChild(svg);

  const maxCol = Math.max(0, ...[...m.nodes.values()].map((n) => n.col));
  canvas.appendChild(UI.el("div", { class: "gcol", style: { left: GPAD + "px" } }, "sources"));
  if (maxCol) canvas.appendChild(UI.el("div", { class: "gcol", style: { left: GPAD + maxCol * GCOLW + "px" } }, "output"));
  if (G.looseY !== null) canvas.appendChild(UI.el("div", { class: "gcol", style: { left: GPAD + "px", top: G.looseY + "px" } }, "not connected"));

  for (const e of m.edges) {
    const a = m.nodes.get(e.from), b = m.nodes.get(e.to);
    const p = document.createElementNS(svgNs, "path");
    p.setAttribute("class", "gedge e-" + e.type);
    const tip = document.createElementNS(svgNs, "title");
    tip.textContent = `${a.title} → ${b.title}${b.inserts[e.toPort] ? " / " + b.inserts[e.toPort].label : ""}: ${e.type}` +
      (e.labels.length ? ` (${e.labels.join(", ")})` : "") + (e.count > 1 ? ` ×${e.count}` : "");
    p.appendChild(tip);
    svg.appendChild(p);
    G.edgeEls.push({ e, el: p });
    const text = e.labels.length ? e.labels.join(", ") + (e.count > 1 ? ` ×${e.count}` : "") : e.count > 1 ? `×${e.count}` : "";
    if (text) {
      const t = document.createElementNS(svgNs, "text");
      t.setAttribute("class", "glabel e-" + e.type);
      t.textContent = text.length > 28 ? text.slice(0, 27) + "…" : text;
      svg.appendChild(t);
      G.edgeEls.push({ e, el: t, label: true });
    }
  }
  G.drawEdges();

  for (const n of m.nodes.values()) {
    const rec = n.rec;
    const attrs = {
      class: `gnode k-${n.kind}` + (rec && rec.disabled ? " off" : "") + (rec ? "" : " ghost") + (rec && DAW.sel === n.path && !DAW.selInsert ? " sel" : ""),
      "data-id": n.id,
      title: (n.members && n.members.length > 1 ? n.members.join("\n") : n.path) + (rec && rec.script ? "\nscript: " + rec.script : ""),
      style: { left: n.x + "px", top: n.y + "px", width: GNODEW + "px", height: n.h + "px" },
      onclick: (ev) => {
        if (G.justDragged) return;
        if (ev.shiftKey || ev.metaKey || ev.ctrlKey) { G.togglePick(n); return; }
        if (G.picked.size) {
          G.picked.clear();
          G.applyPicked();
        }
        if (G.focus === n.id && (!rec || (DAW.sel === n.path && !DAW.selInsert))) G.clearFocus();
        else G.setFocus(n);
      },
      onmouseenter: () => G.hot(n.id),
      onmouseleave: () => G.hot(null)
    };
    if (rec) attrs["data-path"] = n.path;
    const head = UI.el("div", { class: "ghead" },
      UI.el("span", { class: "swatch", style: { background: DAW.color(n.path) } }),
      UI.el("div", { class: "gtext" }, UI.el("div", { class: "gtitle" }, n.title), UI.el("div", { class: "gsub" }, n.sub)));
    if (rec && DAW.has(rec, "Bus")) {
      const free = (rec.comps[A + "Bus"].effects || ["#0"]).findIndex((fx) => DAW.isNone(fx));
      if (free >= 0) {
        head.appendChild(UI.el("button", {
          class: "small gadd", text: "+", title: "Add an insert",
          onclick: (ev) => { ev.stopPropagation(); V.addEffectMenu(ev, n.path, free); }
        }));
      }
    }
    const el = UI.el("div", attrs, head);
    el.addEventListener("pointerdown", (ev) => G.drag(n, el, ev));
    for (const ins of n.inserts) {
      el.appendChild(UI.el("div", {
        class: "gins" + (ins.off ? " off" : "") + (DAW.sel === ins.path && DAW.selInsert ? " sel" : ""), title: `${ins.path}, insert slot ${ins.slot + 1}`,
        "data-path": ins.path,
        style: { borderLeftColor: DAW.color(ins.path) },
        onclick: (ev) => {
          ev.stopPropagation();
          if (G.justDragged) return;
          DAW.select(ins.path, true);
        },
        oncontextmenu: (ev) => {
          ev.preventDefault();
          ev.stopPropagation();
          V.effectMenu(ev, n.path, ins.slot, ins.path);
        }
      }, ins.label));
    }
    if (n.meter && rec) {
      el.appendChild(UI.meter(n.path, { noScale: true }));
      UI.bind(n.path, A + "Bus", ["mute"], (v) => el.classList.toggle("muted", !!v), el);
    }
    canvas.appendChild(el);
  }

  scroller.addEventListener("pointerdown", (ev) => G.marquee(scroller, canvas, ev));
  scroller.addEventListener("click", (ev) => {
    if (ev.target.closest(".gnode") || G.justDragged) return;
    if (G.focus || DAW.sel) G.clearFocus();
  });
  scroller.addEventListener("wheel", (ev) => {
    if (!ev.ctrlKey && !ev.metaKey) return;
    ev.preventDefault();
    const r = scroller.getBoundingClientRect();
    G.setZoom(G.zoom * Math.exp(-ev.deltaY * 0.01), { x: ev.clientX - r.left, y: ev.clientY - r.top });
  }, { passive: false });
  G.setZoom(G.zoom);
  G.applyFocus();
};
