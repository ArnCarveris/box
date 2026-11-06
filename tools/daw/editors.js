"use strict";

const ED = window.ED = { canvases: new Set() };

ED.setupCanvas = (c) => {
  const w = c.clientWidth, h = c.clientHeight;
  const dpr = window.devicePixelRatio || 1;
  if (c.width !== Math.round(w * dpr) || c.height !== Math.round(h * dpr)) {
    c.width = Math.round(w * dpr);
    c.height = Math.round(h * dpr);
  }
  const ctx = c.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  return { ctx, w, h };
};

ED.drawAll = () => {
  for (const c of ED.canvases) {
    if (!c.isConnected) { ED.canvases.delete(c); continue; }
    if (c.draw) c.draw();
  }
};

ED.clipPosition = (clipPath, placementPath) => {
  const t = DAW.single.Transport;
  if (!t) return null;
  const pos = DAW.position();
  const clip = DAW.ents[clipPath];
  if (!clip) return null;
  const c = clip.comps[A + "NoteClip"] || clip.comps[A + "AutomationClip"] || clip.comps[A + "AudioClip"];
  if (!c) return null;
  const placements = placementPath ? [DAW.ents[placementPath]] :
    Object.values(DAW.ents).filter((r) => DAW.has(r, "ClipPlacement") && r.comps[A + "ClipPlacement"].clip === clipPath);
  for (const p of placements) {
    if (!p) continue;
    const cp = p.comps[A + "ClipPlacement"];
    const dur = cp.duration > 0 ? cp.duration : c.length - cp.offset;
    if (pos < cp.start || pos >= cp.start + dur) continue;
    let local = pos - cp.start + cp.offset;
    if (cp.loop && c.length > 0) local = local % c.length;
    return local;
  }
  return null;
};

ED.pianoRoll = (clipPath, opts) => {
  opts = opts || {};
  const comp = A + "NoteClip";
  const c = UI.el("canvas", { class: "roll", tabindex: 0, style: { height: (opts.height || 300) + "px" } });
  const view = { ppb: 40, keyH: 10, x0: 0, top: 84, snap: 0.25, len: 1 };
  const KEYW = 40, VELH = 50;
  let sel = new Set();
  let drag = null;

  const clip = () => {
    const rec = DAW.ents[clipPath];
    return rec ? rec.comps[comp] : null;
  };
  const notes = () => (clip() || {}).notes || [];
  const commit = (list) => {
    const rec = DAW.ents[clipPath];
    if (rec) rec.comps[comp].notes = list;
    DAW.write(clipPath, comp, { notes: list });
  };

  const fit = () => {
    const n = notes();
    if (!n.length) { view.top = 72; return; }
    const hi = Math.max(...n.map((x) => x.pitch));
    const lo = Math.min(...n.map((x) => x.pitch));
    const rows = Math.floor((c.clientHeight - VELH) / view.keyH);
    view.top = Math.min(127, Math.ceil((hi + lo) / 2 + rows / 2));
    const len = (clip() || {}).length || 4;
    const avail = c.clientWidth - KEYW;
    if (avail > 0) view.ppb = Math.max(4, Math.min(120, avail / len));
  };

  const yOf = (p) => (view.top - p) * view.keyH;
  const pOf = (y) => Math.floor(view.top - y / view.keyH) + 1;
  const xOf = (b) => KEYW + (b - view.x0) * view.ppb;
  const bOf = (x) => (x - KEYW) / view.ppb + view.x0;
  const snap = (b) => Math.round(b / view.snap) * view.snap;

  const hit = (x, y) => {
    const list = notes();
    for (let i = list.length - 1; i >= 0; i--) {
      const n = list[i];
      const x0 = xOf(n.start), x1 = xOf(n.start + n.duration);
      if (x >= x0 && x <= x1 && y >= yOf(n.pitch) && y < yOf(n.pitch) + view.keyH) {
        return { i, edge: x1 - x < 6 };
      }
    }
    return null;
  };

  c.draw = () => {
    const { ctx, w, h } = ED.setupCanvas(c);
    if (!w) return;
    if (!view.fitted && w > 0) { fit(); view.fitted = true; }
    const cl = clip();
    const gridH = h - VELH;
    ctx.fillStyle = "#101215";
    ctx.fillRect(0, 0, w, h);
    const rows = Math.ceil(gridH / view.keyH) + 1;
    for (let r = 0; r < rows; r++) {
      const p = view.top - r;
      const y = r * view.keyH;
      const black = [1, 3, 6, 8, 10].includes(((p % 12) + 12) % 12);
      ctx.fillStyle = black ? "#16191d" : "#1c2025";
      ctx.fillRect(KEYW, y, w - KEYW, view.keyH);
      ctx.fillStyle = black ? "#222" : "#d8d8d8";
      ctx.fillRect(0, y, KEYW - 2, view.keyH - 1);
      if (((p % 12) + 12) % 12 === 0) {
        ctx.fillStyle = "#555";
        ctx.font = "9px sans-serif";
        ctx.fillText(UI.noteName(p), 3, y + view.keyH - 2);
        ctx.fillStyle = "#2a2f35";
        ctx.fillRect(KEYW, y + view.keyH - 1, w - KEYW, 1);
      }
    }
    const len = cl ? cl.length : 4;
    const bpb = (DAW.single.Transport || {}).beats_per_bar || 4;
    const b0 = Math.floor(view.x0), b1 = bOf(w);
    for (let b = b0; b <= b1; b += view.snap) {
      const x = xOf(b);
      if (x < KEYW) continue;
      const isBar = Math.abs(b % bpb) < 1e-6;
      const isBeat = Math.abs(b % 1) < 1e-6;
      ctx.fillStyle = isBar ? "#4a525c" : isBeat ? "#30363d" : "#22272c";
      if (view.ppb * view.snap < 5 && !isBeat) continue;
      ctx.fillRect(Math.round(x), 0, 1, gridH);
      if (isBar) {
        ctx.fillStyle = "#7d858f";
        ctx.font = "9px sans-serif";
        ctx.fillText(String(b / bpb + 1), x + 2, 9);
      }
    }
    const xl = xOf(len);
    ctx.fillStyle = "rgba(0,0,0,0.45)";
    if (xl < w) ctx.fillRect(Math.max(xl, KEYW), 0, w - Math.max(xl, KEYW), h);
    ctx.fillStyle = "#ff9f2e";
    ctx.fillRect(xl, 0, 2, gridH);
    const list = notes();
    list.forEach((n, i) => {
      const x = xOf(n.start), y = yOf(n.pitch);
      const nw = Math.max(2, n.duration * view.ppb);
      const on = sel.has(i);
      const v = n.velocity === undefined ? 1 : n.velocity;
      ctx.fillStyle = on ? "#ffd54f" : `hsl(28, 100%, ${35 + v * 30}%)`;
      ctx.fillRect(x, y + 1, nw - 1, view.keyH - 2);
      ctx.strokeStyle = on ? "#fff" : "rgba(0,0,0,0.5)";
      ctx.strokeRect(x + 0.5, y + 1.5, nw - 2, view.keyH - 3);
      const vy = gridH + VELH - v * (VELH - 6);
      ctx.fillStyle = on ? "#ffd54f" : "#ff9f2e";
      ctx.fillRect(x, vy, 2, gridH + VELH - vy);
      ctx.fillRect(x, vy, 6, 2);
    });
    ctx.fillStyle = "#2c3137";
    ctx.fillRect(0, gridH, w, 1);
    ctx.fillStyle = "#7d858f";
    ctx.font = "9px sans-serif";
    ctx.fillText("vel", 4, gridH + 12);
    const lp = ED.clipPosition(clipPath, opts.placement);
    if (lp !== null) {
      const x = xOf(lp);
      ctx.fillStyle = "#4fc3f7";
      ctx.fillRect(x, 0, 1.5, h);
    }
    if (drag && drag.kind === "box") {
      ctx.strokeStyle = "#4fc3f7";
      ctx.strokeRect(Math.min(drag.x0, drag.x1), Math.min(drag.y0, drag.y1), Math.abs(drag.x1 - drag.x0), Math.abs(drag.y1 - drag.y0));
    }
  };
  ED.canvases.add(c);

  const local = (e) => {
    const r = c.getBoundingClientRect();
    return { x: e.clientX - r.left, y: e.clientY - r.top };
  };

  c.addEventListener("pointerdown", (e) => {
    c.focus();
    c.setPointerCapture(e.pointerId);
    start(e);
    if (drag) DAW.dragging++;
  });

  const start = (e) => {
    const { x, y } = local(e);
    const gridH = c.clientHeight - VELH;
    if (x < KEYW && y < gridH) {
      const p = pOf(y);
      if (opts.audition) { DAW.noteOn(p, opts.audition(), 0.8); drag = { kind: "key", p }; }
      return;
    }
    if (y >= gridH) {
      drag = { kind: "vel" };
      setVel(x, y);
      return;
    }
    const h = hit(x, y);
    const list = notes();
    if (h) {
      if (e.shiftKey) { if (sel.has(h.i)) sel.delete(h.i); else sel.add(h.i); }
      else if (!sel.has(h.i)) sel = new Set([h.i]);
      if (e.altKey) {
        const copies = [...sel].map((i) => DAW.clone(list[i]));
        const nl = list.concat(copies);
        sel = new Set(copies.map((_, k) => list.length + k));
        commit(nl);
      }
      const n = notes()[h.i] || list[h.i];
      view.len = n.duration;
      drag = {
        kind: h.edge ? "resize" : "move", x, y, b: bOf(x), p: pOf(y),
        orig: notes().map((n) => ({ start: n.start, pitch: n.pitch, duration: n.duration }))
      };
      if (opts.audition && !h.edge) DAW.noteOn(n.pitch, opts.audition(), n.velocity);
      drag.auditionPitch = h.edge ? null : n.pitch;
    } else {
      if (e.detail >= 2) {
        const b = Math.max(0, Math.floor(bOf(x) / view.snap) * view.snap);
        const p = pOf(y);
        const nl = notes().concat([{
          start: b, duration: view.len, pitch: p, velocity: 0.8, pan: 0,
          bend: { kind: "CurveLinear", keys: [] }, mod: { kind: "CurveLinear", keys: [] },
          pressure: { kind: "CurveLinear", keys: [] }
        }]);
        sel = new Set([nl.length - 1]);
        commit(nl);
        drag = null;
        return;
      }
      if (!e.shiftKey) sel = new Set();
      drag = { kind: "box", x0: x, y0: y, x1: x, y1: y };
    }
  };

  const setVel = (x, y) => {
    const gridH = c.clientHeight - VELH;
    const v = Math.max(0.01, Math.min(1, (gridH + VELH - y) / (VELH - 6)));
    const list = DAW.clone(notes());
    let changed = false;
    list.forEach((n, i) => {
      const x0 = xOf(n.start);
      const target = sel.size ? sel.has(i) && Math.abs(x0 - x) < 8 : Math.abs(x0 - x) < 5;
      if (target || (sel.size && sel.has(i) && drag && drag.all)) { n.velocity = Math.round(v * 100) / 100; changed = true; }
    });
    if (changed) commit(list);
  };

  c.addEventListener("pointermove", (e) => {
    const { x, y } = local(e);
    if (!drag) {
      const h = hit(x, y);
      c.style.cursor = h ? (h.edge ? "ew-resize" : "move") : (x < KEYW ? "pointer" : "default");
      return;
    }
    if (drag.kind === "vel") { setVel(x, y); return; }
    if (drag.kind === "box") { drag.x1 = x; drag.y1 = y; return; }
    if (drag.kind === "key") return;
    const db = snap(bOf(x) - drag.b);
    const dp = pOf(y) - drag.p;
    const list = DAW.clone(notes());
    for (const i of sel) {
      const o = drag.orig[i];
      if (!o || !list[i]) continue;
      if (drag.kind === "move") {
        list[i].start = Math.max(0, o.start + db);
        list[i].pitch = Math.max(0, Math.min(127, o.pitch + dp));
      } else {
        list[i].duration = Math.max(view.snap, o.duration + db);
        view.len = list[i].duration;
      }
    }
    if (drag.kind === "move" && dp !== drag.lastDp && opts.audition && drag.auditionPitch !== null) {
      if (drag.lastDp !== undefined) DAW.noteOff(drag.auditionPitch + drag.lastDp);
      DAW.noteOn(drag.auditionPitch + dp, opts.audition(), 0.7);
      drag.lastDp = dp;
    }
    commit(list);
  });

  const end = () => {
    if (!drag) { return; }
    DAW.dragging = Math.max(0, DAW.dragging - 1);
    if (drag.kind === "box") {
      const bx0 = bOf(Math.min(drag.x0, drag.x1)), bx1 = bOf(Math.max(drag.x0, drag.x1));
      const p0 = pOf(Math.max(drag.y0, drag.y1)), p1 = pOf(Math.min(drag.y0, drag.y1));
      notes().forEach((n, i) => {
        if (n.start + n.duration >= bx0 && n.start <= bx1 && n.pitch >= p0 && n.pitch <= p1) sel.add(i);
      });
    }
    if (drag.kind === "key") DAW.noteOff(drag.p);
    if (drag.kind === "move" && drag.auditionPitch !== null && drag.auditionPitch !== undefined) {
      DAW.noteOff(drag.auditionPitch + (drag.lastDp || 0));
      DAW.noteOff(drag.auditionPitch);
    }
    drag = null;
  };
  c.addEventListener("pointerup", end);
  c.addEventListener("pointercancel", end);

  c.addEventListener("wheel", (e) => {
    e.preventDefault();
    if (e.ctrlKey || e.metaKey) {
      const { x } = local(e);
      const b = bOf(x);
      view.ppb = Math.max(4, Math.min(400, view.ppb * Math.exp(-e.deltaY * 0.01)));
      view.x0 = Math.max(0, b - (x - KEYW) / view.ppb);
    } else if (e.shiftKey || Math.abs(e.deltaX) > Math.abs(e.deltaY)) {
      view.x0 = Math.max(0, view.x0 + (e.deltaX || e.deltaY) / view.ppb);
    } else {
      view.top = Math.max(12, Math.min(127, view.top - Math.sign(e.deltaY) * 2));
    }
  }, { passive: false });

  c.addEventListener("keydown", (e) => {
    const list = notes();
    if ((e.key === "Delete" || e.key === "Backspace") && sel.size) {
      commit(list.filter((_, i) => !sel.has(i)));
      sel = new Set();
      e.preventDefault();
      e.stopPropagation();
    } else if ((e.key === "a") && (e.metaKey || e.ctrlKey)) {
      sel = new Set(list.map((_, i) => i));
      e.preventDefault();
      e.stopPropagation();
    } else if (["ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight"].includes(e.key) && sel.size) {
      const nl = DAW.clone(list);
      const oct = e.shiftKey ? 12 : 1;
      for (const i of sel) {
        if (e.key === "ArrowUp") nl[i].pitch = Math.min(127, nl[i].pitch + oct);
        if (e.key === "ArrowDown") nl[i].pitch = Math.max(0, nl[i].pitch - oct);
        if (e.key === "ArrowLeft") nl[i].start = Math.max(0, nl[i].start - view.snap);
        if (e.key === "ArrowRight") nl[i].start = nl[i].start + view.snap;
      }
      commit(nl);
      e.preventDefault();
      e.stopPropagation();
    }
  });

  c.view = view;
  c.fit = fit;
  return c;
};

ED.curveEditor = (clipPath, opts) => {
  opts = opts || {};
  const comp = A + "AutomationClip";
  const c = UI.el("canvas", { class: "roll", tabindex: 0, style: { height: (opts.height || 220) + "px" } });
  let sel = -1, drag = null;
  const PAD = 14;
  const clip = () => (DAW.ents[clipPath] || { comps: {} }).comps[comp];
  const range = () => {
    const cl = clip();
    if (!cl) return { min: 0, max: 1 };
    const m = UI.meta(DAW.short(cl.component), (cl.member || "").replace(/\[.*$/, "").split(".").pop());
    const keys = (cl.curve && cl.curve.keys) || [];
    let lo = m.min, hi = m.max;
    for (const k of keys) { lo = Math.min(lo, k.value); hi = Math.max(hi, k.value); }
    if (hi - lo < 1e-6) hi = lo + 1;
    return { min: lo, max: hi, log: m.log && lo > 0, meta: m };
  };
  const geo = () => {
    const cl = clip();
    const len = cl ? cl.length || 4 : 4;
    const w = c.clientWidth, h = c.clientHeight;
    const r = range();
    const tn = (v) => r.log ? Math.log(v / r.min) / Math.log(r.max / r.min) : (v - r.min) / (r.max - r.min);
    const fn = (n) => r.log ? r.min * Math.pow(r.max / r.min, n) : r.min + n * (r.max - r.min);
    return {
      len, w, h, r,
      x: (t) => PAD + t / len * (w - 2 * PAD),
      t: (x) => (x - PAD) / (w - 2 * PAD) * len,
      y: (v) => h - PAD - tn(v) * (h - 2 * PAD),
      v: (y) => fn(Math.max(0, Math.min(1, (h - PAD - y) / (h - 2 * PAD))))
    };
  };
  const commit = (keys) => {
    const cl = clip();
    if (!cl) return;
    const curve = { kind: cl.curve.kind, keys };
    cl.curve = curve;
    DAW.write(clipPath, comp, { curve });
  };
  c.draw = () => {
    const { ctx, w, h } = ED.setupCanvas(c);
    if (!w) return;
    const g = geo();
    ctx.fillStyle = "#101215";
    ctx.fillRect(0, 0, w, h);
    const bpb = (DAW.single.Transport || {}).beats_per_bar || 4;
    for (let b = 0; b <= g.len; b++) {
      ctx.fillStyle = b % bpb === 0 ? "#3a4048" : "#22272c";
      ctx.fillRect(Math.round(g.x(b)), 0, 1, h);
    }
    ctx.fillStyle = "#7d858f";
    ctx.font = "9px sans-serif";
    ctx.fillText(UI.fmt(g.r.max, g.r.meta), 3, 10);
    ctx.fillText(UI.fmt(g.r.min, g.r.meta), 3, h - 3);
    const cl = clip();
    const keys = cl && cl.curve ? cl.curve.keys : [];
    ctx.strokeStyle = "#4fc3f7";
    ctx.lineWidth = 2;
    ctx.beginPath();
    keys.forEach((k, i) => {
      const x = g.x(k.time), y = g.y(k.value);
      if (i === 0) { ctx.moveTo(PAD, y); ctx.lineTo(x, y); } else ctx.lineTo(x, y);
    });
    if (keys.length) ctx.lineTo(w - PAD, g.y(keys[keys.length - 1].value));
    ctx.stroke();
    ctx.lineWidth = 1;
    keys.forEach((k, i) => {
      ctx.fillStyle = i === sel ? "#ffd54f" : "#4fc3f7";
      ctx.beginPath();
      ctx.arc(g.x(k.time), g.y(k.value), 4, 0, Math.PI * 2);
      ctx.fill();
    });
    const lp = ED.clipPosition(clipPath, opts.placement);
    if (lp !== null) {
      ctx.fillStyle = "#ff9f2e";
      ctx.fillRect(g.x(lp), 0, 1.5, h);
    }
    if (sel >= 0 && keys[sel]) {
      ctx.fillStyle = "#e6e9ed";
      ctx.fillText(`beat ${keys[sel].time.toFixed(2)}  ${UI.fmt(keys[sel].value, g.r.meta)}`, w - 160, 12);
    }
  };
  ED.canvases.add(c);
  const local = (e) => { const r = c.getBoundingClientRect(); return { x: e.clientX - r.left, y: e.clientY - r.top }; };
  c.addEventListener("pointerdown", (e) => {
    c.focus();
    const { x, y } = local(e);
    const g = geo();
    const cl = clip();
    if (!cl) return;
    const keys = DAW.clone(cl.curve.keys || []);
    sel = keys.findIndex((k) => Math.hypot(g.x(k.time) - x, g.y(k.value) - y) < 7);
    if (sel < 0 && e.detail >= 2) {
      const t = Math.max(0, Math.min(g.len, Math.round(g.t(x) * 16) / 16));
      if (keys.some((k) => Math.abs(k.time - t) < 1e-4)) return;
      keys.push({ time: t, value: g.v(y), in: 0, out: 0 });
      keys.sort((a, b) => a.time - b.time);
      sel = keys.findIndex((k) => k.time === t);
      commit(keys);
    }
    if (sel >= 0) {
      c.setPointerCapture(e.pointerId);
      drag = true;
      DAW.dragging++;
    }
  });
  c.addEventListener("pointermove", (e) => {
    if (!drag) return;
    const { x, y } = local(e);
    const g = geo();
    const keys = DAW.clone(clip().curve.keys);
    const prev = keys[sel - 1], next = keys[sel + 1];
    let t = Math.round(g.t(x) * (e.shiftKey ? 64 : 16)) / (e.shiftKey ? 64 : 16);
    if (prev) t = Math.max(prev.time + 1 / 64, t);
    if (next) t = Math.min(next.time - 1 / 64, t);
    keys[sel].time = Math.max(0, t);
    keys[sel].value = g.v(y);
    commit(keys);
  });
  const end = () => { if (drag) { drag = null; DAW.dragging = Math.max(0, DAW.dragging - 1); } };
  c.addEventListener("pointerup", end);
  c.addEventListener("pointercancel", end);
  c.addEventListener("keydown", (e) => {
    if ((e.key === "Delete" || e.key === "Backspace") && sel >= 0) {
      const keys = DAW.clone(clip().curve.keys);
      keys.splice(sel, 1);
      sel = -1;
      commit(keys);
      e.preventDefault();
      e.stopPropagation();
    }
  });
  return c;
};

function biquadMag(kind, f0, gain, q, f, sr) {
  const A2 = Math.pow(10, gain / 40);
  const w0 = 2 * Math.PI * f0 / sr;
  const cs = Math.cos(w0), sn = Math.sin(w0);
  const alpha = sn / (2 * q);
  let b0, b1, b2, a0, a1, a2;
  if (kind === "AudioEqPeak") {
    b0 = 1 + alpha * A2; b1 = -2 * cs; b2 = 1 - alpha * A2;
    a0 = 1 + alpha / A2; a1 = -2 * cs; a2 = 1 - alpha / A2;
  } else if (kind === "AudioEqLowShelf" || kind === "AudioEqHighShelf") {
    const sq = 2 * Math.sqrt(A2) * alpha;
    const s = kind === "AudioEqLowShelf" ? 1 : -1;
    b0 = A2 * ((A2 + 1) - s * (A2 - 1) * cs + sq);
    b1 = s * 2 * A2 * ((A2 - 1) - s * (A2 + 1) * cs);
    b2 = A2 * ((A2 + 1) - s * (A2 - 1) * cs - sq);
    a0 = (A2 + 1) + s * (A2 - 1) * cs + sq;
    a1 = -s * 2 * ((A2 - 1) + s * (A2 + 1) * cs);
    a2 = (A2 + 1) + s * (A2 - 1) * cs - sq;
  } else if (kind === "AudioEqLowCut" || kind === "SynthHighPass") {
    b0 = (1 + cs) / 2; b1 = -(1 + cs); b2 = (1 + cs) / 2;
    a0 = 1 + alpha; a1 = -2 * cs; a2 = 1 - alpha;
  } else if (kind === "AudioEqHighCut" || kind === "SynthLowPass") {
    b0 = (1 - cs) / 2; b1 = 1 - cs; b2 = (1 - cs) / 2;
    a0 = 1 + alpha; a1 = -2 * cs; a2 = 1 - alpha;
  } else if (kind === "SynthBandPass") {
    b0 = alpha; b1 = 0; b2 = -alpha;
    a0 = 1 + alpha; a1 = -2 * cs; a2 = 1 - alpha;
  } else if (kind === "SynthNotch") {
    b0 = 1; b1 = -2 * cs; b2 = 1;
    a0 = 1 + alpha; a1 = -2 * cs; a2 = 1 - alpha;
  } else {
    return 1;
  }
  const w = 2 * Math.PI * f / sr;
  const cr = Math.cos(w), ci = -Math.sin(w), c2r = Math.cos(2 * w), c2i = -Math.sin(2 * w);
  const nr = b0 + b1 * cr + b2 * c2r, ni = b1 * ci + b2 * c2i;
  const dr = a0 + a1 * cr + a2 * c2r, di = a1 * ci + a2 * c2i;
  return Math.sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
}

ED.responseGraph = (path, comp) => {
  const c = UI.el("canvas", { class: "graph", style: { width: "100%", height: "90px" } });
  c.draw = () => {
    const { ctx, w, h } = ED.setupCanvas(c);
    if (!w) return;
    const rec = DAW.ents[path];
    const v = rec && rec.comps[comp];
    ctx.fillStyle = "#0c0e10";
    ctx.fillRect(0, 0, w, h);
    if (!v) return;
    const sr = (DAW.single.AudioStatus || {}).sample_rate || 48000;
    const fx = (f) => Math.log(f / 20) / Math.log(20000 / 20) * w;
    const dbr = 24;
    const fy = (db) => h / 2 - db / dbr * (h / 2 - 4);
    ctx.fillStyle = "#22272c";
    for (const f of [50, 100, 200, 500, 1000, 2000, 5000, 10000]) ctx.fillRect(fx(f), 0, 1, h);
    for (const db of [-12, 0, 12]) ctx.fillRect(0, fy(db), w, 1);
    const stages = [];
    if (comp === A + "Equalizer") {
      for (const b of v.bands || []) if (b.kind !== "AudioEqOff") stages.push([b.kind, b.frequency, b.gain, b.q]);
    } else if (comp === A + "AudioFilter") {
      const q = 0.7071 + (v.resonance || 0) * 8;
      stages.push([v.kind, v.cutoff, 0, q]);
      if (v.slope === "SynthSlope24") stages.push([v.kind, v.cutoff, 0, 0.7071]);
    }
    ctx.strokeStyle = "#ff9f2e";
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    for (let x = 0; x <= w; x += 2) {
      const f = 20 * Math.pow(1000, x / w);
      let mag = 1;
      for (const s of stages) mag *= biquadMag(s[0], Math.min(s[1], sr * 0.49), s[2], Math.max(0.1, s[3]), f, sr);
      if (comp === A + "AudioFilter") {
        const mix = v.mix === undefined ? 1 : v.mix;
        mag = mix * mag + (1 - mix);
      }
      const y = fy(Math.max(-dbr, Math.min(dbr, 20 * Math.log10(Math.max(mag, 1e-6)))));
      if (x === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    }
    ctx.stroke();
    ctx.lineWidth = 1;
    if (comp === A + "Equalizer") {
      (v.bands || []).forEach((b, i) => {
        if (b.kind === "AudioEqOff") return;
        ctx.fillStyle = "#4fc3f7";
        ctx.beginPath();
        ctx.arc(fx(b.frequency), fy(["AudioEqLowCut", "AudioEqHighCut"].includes(b.kind) ? 0 : b.gain), 4, 0, Math.PI * 2);
        ctx.fill();
        ctx.fillStyle = "#0c0e10";
        ctx.font = "8px sans-serif";
        ctx.fillText(String(i + 1), fx(b.frequency) - 2, fy(["AudioEqLowCut", "AudioEqHighCut"].includes(b.kind) ? 0 : b.gain) + 3);
      });
    }
  };
  if (comp === A + "Equalizer") {
    let drag = -1;
    c.addEventListener("pointerdown", (e) => {
      const rec = DAW.ents[path];
      const v = rec && rec.comps[comp];
      if (!v) return;
      const r = c.getBoundingClientRect();
      const x = e.clientX - r.left, y = e.clientY - r.top;
      const w = r.width, h = r.height;
      const fx = (f) => Math.log(f / 20) / Math.log(1000) * w;
      const fy = (db) => h / 2 - db / 24 * (h / 2 - 4);
      drag = (v.bands || []).findIndex((b) => b.kind !== "AudioEqOff" &&
        Math.hypot(fx(b.frequency) - x, fy(["AudioEqLowCut", "AudioEqHighCut"].includes(b.kind) ? 0 : b.gain) - y) < 8);
      if (drag >= 0) { c.setPointerCapture(e.pointerId); DAW.dragging++; }
    });
    c.addEventListener("pointermove", (e) => {
      if (drag < 0) return;
      const rec = DAW.ents[path];
      const v = DAW.clone(rec.comps[comp]);
      const r = c.getBoundingClientRect();
      const x = e.clientX - r.left, y = e.clientY - r.top;
      const b = v.bands[drag];
      b.frequency = Math.round(20 * Math.pow(1000, Math.max(0, Math.min(1, x / r.width))));
      if (!["AudioEqLowCut", "AudioEqHighCut"].includes(b.kind)) {
        b.gain = Math.round(Math.max(-24, Math.min(24, (r.height / 2 - y) / (r.height / 2 - 4) * 24)) * 10) / 10;
      }
      DAW.write(path, comp, { bands: v.bands });
    });
    const end = () => { if (drag >= 0) { drag = -1; DAW.dragging = Math.max(0, DAW.dragging - 1); } };
    c.addEventListener("pointerup", end);
    c.addEventListener("pointercancel", end);
    c.title = "Drag a band point to move its frequency and gain";
  }
  ED.canvases.add(c);
  return c;
};

ED.envelopeGraph = (path, comp) => {
  const c = UI.el("canvas", { class: "graph", style: { width: "100%", height: "60px" } });
  c.draw = () => {
    const { ctx, w, h } = ED.setupCanvas(c);
    if (!w) return;
    const rec = DAW.ents[path];
    const v = rec && rec.comps[comp];
    ctx.fillStyle = "#0c0e10";
    ctx.fillRect(0, 0, w, h);
    if (!v) return;
    const a = v.attack || 0, hd = v.hold || 0, d = v.decay || 0, r = v.release || 0;
    const s = v.sustain === undefined ? 0 : v.sustain;
    const sus = Math.max(0.3, (a + d + r) * 0.3);
    const total = a + d + hd + sus + r || 1;
    const X = (t) => 4 + t / total * (w - 8);
    const Y = (l) => h - 4 - l * (h - 8);
    const shape = (x0, l0, dur, l1, curve) => {
      const n = 16;
      for (let i = 1; i <= n; i++) {
        const u = i / n;
        const k = curve ? (1 - Math.exp(-curve * u)) / (1 - Math.exp(-curve)) : u;
        ctx.lineTo(X(x0 + dur * u), Y(l0 + (l1 - l0) * k));
      }
    };
    ctx.strokeStyle = "#ff9f2e";
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.moveTo(X(0), Y(0));
    shape(0, 0, a, 1, v.attack_curve || 0);
    shape(a, 1, d, s, v.curve || 0);
    ctx.lineTo(X(a + d + hd + sus), Y(s));
    shape(a + d + hd + sus, s, r, 0, v.curve || 0);
    ctx.stroke();
    ctx.lineWidth = 1;
    ctx.fillStyle = "#3a4048";
    ctx.fillRect(X(a + d + hd), 0, 1, h);
    ctx.fillRect(X(a + d + hd + sus), 0, 1, h);
  };
  ED.canvases.add(c);
  return c;
};

const KEYMAP = { a: 0, w: 1, s: 2, e: 3, d: 4, f: 5, t: 6, g: 7, y: 8, h: 9, u: 10, j: 11, k: 12, o: 13, l: 14, p: 15, ";": 16 };

ED.keyboard = (target) => {
  const st = ED.kbd || (ED.kbd = { octave: 4, velocity: 0.8 });
  const keys = UI.el("div", { class: "keys" });
  const els = {};
  const base = () => (st.octave + 1) * 12;
  const build = () => {
    keys.textContent = "";
    let wx = 0;
    for (let i = 0; i < 25; i++) {
      const p = base() + i;
      const black = [1, 3, 6, 8, 10].includes(i % 12);
      const k = UI.el("div", { class: black ? "black" : "white" });
      if (black) k.style.left = (wx * 22 - 7) + "px";
      else {
        wx++;
        if (i % 12 === 0) k.appendChild(UI.el("span", {}, UI.noteName(p)));
      }
      const down = (e) => {
        e.preventDefault();
        k.setPointerCapture(e.pointerId);
        k.classList.add("down");
        DAW.noteOn(p, target(), st.velocity);
      };
      const up = () => { k.classList.remove("down"); DAW.noteOff(p); };
      k.addEventListener("pointerdown", down);
      k.addEventListener("pointerup", up);
      k.addEventListener("pointercancel", up);
      els[p] = k;
      keys.appendChild(k);
    }
  };
  build();
  const oct = UI.el("span", {}, `C${st.octave}`);
  const info = UI.el("div", { class: "kbd-info" },
    UI.el("div", {}, "Play with A W S E D F T G Y H U J K, Z / X for the octave"),
    UI.el("div", { class: "row" },
      UI.el("button", { class: "small", text: "oct -", onclick: () => { st.octave = Math.max(0, st.octave - 1); oct.textContent = `C${st.octave}`; build(); } }),
      oct,
      UI.el("button", { class: "small", text: "oct +", onclick: () => { st.octave = Math.min(8, st.octave + 1); oct.textContent = `C${st.octave}`; build(); } }),
      UI.knob({
        label: "velocity", value: st.velocity, meta: { min: 0.05, max: 1, def: 0.8 },
        onchange: (v) => { st.velocity = v; }
      })));
  const el = UI.el("div", { class: "keyboard" }, keys, info);
  ED.activeKeyboard = {
    target,
    press: (i, on) => {
      const p = base() + i;
      if (on) { DAW.noteOn(p, target(), st.velocity); if (els[p]) els[p].classList.add("down"); }
      else { DAW.noteOff(p); if (els[p]) els[p].classList.remove("down"); }
    },
    octave: (d) => { st.octave = Math.max(0, Math.min(8, st.octave + d)); oct.textContent = `C${st.octave}`; build(); }
  };
  return el;
};

ED.held = {};

document.addEventListener("keydown", (e) => {
  if (e.repeat || e.metaKey || e.ctrlKey || e.altKey) return;
  const t = e.target;
  if (t && (t.tagName === "INPUT" || t.tagName === "SELECT" || t.tagName === "TEXTAREA")) return;
  const kb = ED.activeKeyboard;
  if (!kb || !kb.target()) return;
  const k = e.key.toLowerCase();
  if (k === "z") { kb.octave(-1); return; }
  if (k === "x") { kb.octave(1); return; }
  if (KEYMAP[k] === undefined || ED.held[k] !== undefined) return;
  ED.held[k] = KEYMAP[k];
  kb.press(KEYMAP[k], true);
});

document.addEventListener("keyup", (e) => {
  const k = e.key.toLowerCase();
  if (ED.held[k] === undefined) return;
  const kb = ED.activeKeyboard;
  if (kb) kb.press(ED.held[k], false);
  delete ED.held[k];
});
