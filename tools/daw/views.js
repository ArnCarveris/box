"use strict";

const V = window.V = {
  arr: { ppb: 24, x0: 0, follow: true, snap: 1 }
};

const ROWH = 52, RULERH = 22;

V.trackOrder = () => {
  const tracks = DAW.tracks();
  const lanes = ((DAW.single.Song || {}).lanes || []).map((l) => l.bus);
  const idx = (r) => { const i = lanes.indexOf(r.path); return i < 0 ? 1000 + r.order : i; };
  return tracks.sort((a, b) => idx(a) - idx(b));
};

V.laneName = (path) => {
  const lane = ((DAW.single.Song || {}).lanes || []).find((l) => l.bus === path);
  return lane ? lane.name : DAW.leaf(path);
};

V.auditionFor = (path) => {
  const rec = DAW.ents[path];
  if (!rec) return null;
  const kind = DAW.kindOf(rec);
  const fromInstrument = (instPath, bus) => {
    const inst = DAW.val(instPath, "Instrument");
    if (!inst || DAW.isNone(inst.sound)) return null;
    return { sound: inst.sound, root: inst.root, velocity_curve: inst.velocity_curve, bus: bus || "#0" };
  };
  if (kind === "track") {
    const t = rec.comps[A + "Track"];
    return DAW.isNone(t.instrument) ? null : fromInstrument(t.instrument, rec.path);
  }
  if (kind === "placement") return V.auditionFor(rec.parent);
  if (kind === "instrument") {
    const track = DAW.tracks().find((t) => t.comps[A + "Track"].instrument === path);
    return fromInstrument(path, track ? track.path : null);
  }
  if (kind === "sound") {
    const out = rec.comps[A + "AudioOutput"];
    const inst = DAW.list("Instrument").find((i) => i.comps[A + "Instrument"].sound === path);
    if (inst) {
      const a = V.auditionFor(inst.path);
      if (a) return a;
    }
    return { sound: path, root: 69, velocity_curve: 2, bus: out && !DAW.isNone(out.bus) ? out.bus : "#0" };
  }
  if (kind === "layer") return V.auditionFor(rec.parent);
  if (kind === "clip") {
    const p = Object.values(DAW.ents).find((r) => DAW.has(r, "ClipPlacement") && r.comps[A + "ClipPlacement"].clip === path);
    return p ? V.auditionFor(p.parent) : null;
  }
  return null;
};

V.renderView = () => {
  const el = document.getElementById("view");
  const keep = el.querySelector(".mixer, .arrange-body, .macro-grid, .graph");
  const scroll = keep ? [keep.scrollLeft, keep.scrollTop] : [0, 0];
  el.textContent = "";
  if (!DAW.conn) {
    el.appendChild(UI.el("div", { class: "placeholder" }, "Looking for flecs apps on localhost:27750 and up.", UI.el("br"),
      "Start one with ", UI.el("code", {}, "bake3 run apps/boxrun --local-env -- --port 27750 etc/scenes/jukebox.flecs")));
    return;
  }
  if (!DAW.connected) {
    el.appendChild(UI.el("div", { class: "placeholder" }, `Connecting to ${DAW.host}...`));
    return;
  }
  if (!DAW.hasAudio) {
    el.appendChild(UI.el("div", { class: "placeholder" }, `${DAW.host} has no box.audio module.`));
    return;
  }
  if (DAW.view === "graph") G.render(el);
  else if (DAW.view === "arrange") V.arrange(el);
  else if (DAW.view === "mixer") V.mixer(el);
  else V.modulation(el);
  const now = el.querySelector(".mixer, .arrange-body, .macro-grid, .graph");
  if (now) { now.scrollLeft = scroll[0]; now.scrollTop = scroll[1]; }
};

V.arrange = (root) => {
  const tracks = V.trackOrder();
  const st = V.arr;
  const tools = UI.el("div", { class: "arrange-tools" },
    UI.el("span", { style: { color: "var(--fg2)" } }, "snap"),
    UI.select({
      options: [{ value: "4", label: "bar" }, { value: "1", label: "beat" }, { value: "0.5", label: "1/8" }, { value: "0.25", label: "1/16" }],
      value: String(st.snap), onchange: (v) => { st.snap = parseFloat(v); }
    }),
    UI.toggle({ label: "follow", value: st.follow, onchange: (v) => { st.follow = v; } }),
    UI.el("button", { class: "small", text: "fit", onclick: () => V.fitArrange() }),
    UI.el("span", { class: "grow" }),
    UI.el("span", { style: { color: "var(--fg2)" } },
      "click ruler to seek, shift+drag ruler for the loop, drag clips to move, double click opens, Delete removes, Cmd+D duplicates"));
  const heads = UI.el("div", { class: "track-heads" }, UI.el("div", { class: "ruler-head" }));
  const lanes = UI.el("div", { class: "lanes" });
  const canvas = UI.el("canvas", { tabindex: 0 });
  lanes.appendChild(canvas);
  const body = UI.el("div", { class: "arrange-body" }, heads, lanes);
  root.appendChild(UI.el("div", { class: "arrange" }, tools, body));
  if (!tracks.length) {
    lanes.appendChild(UI.el("div", { class: "placeholder" }, "No tracks. Load a song in the bar above."));
  }
  for (const t of tracks) {
    const bus = t.comps[A + "AudioBus"];
    const tr = t.comps[A + "Track"];
    const mute = UI.toggle({ label: "M", cls: "small mute", value: bus.mute, onchange: (v) => DAW.write(t.path, A + "AudioBus", { mute: v }) });
    const solo = UI.toggle({ label: "S", cls: "small solo", value: bus.solo, onchange: (v) => DAW.write(t.path, A + "AudioBus", { solo: v }) });
    UI.bind(t.path, A + "AudioBus", ["mute"], (v) => mute.set(v), mute);
    UI.bind(t.path, A + "AudioBus", ["solo"], (v) => solo.set(v), solo);
    const meter = UI.meter(t.path, { noScale: true });
    meter.style.height = (ROWH - 8) + "px";
    heads.appendChild(UI.el("div", {
      class: "track-head" + (DAW.sel === t.path ? " sel" : ""), style: { height: ROWH + "px" }, "data-path": t.path,
      title: "click selects, double click opens the track in its own window",
      onclick: () => DAW.select(t.path),
      ondblclick: () => DAW.openWindow(t.path)
    }, UI.el("div", { class: "color", style: { background: DAW.color(t.path) } }),
      UI.el("div", { class: "tname" }, UI.el("div", {}, V.laneName(t.path)),
        UI.el("div", { class: "tsub" }, DAW.isNone(tr.instrument) ? t.name : DAW.leaf(tr.instrument))),
      mute, solo, meter));
  }
  canvas.style.height = (RULERH + tracks.length * ROWH + 40) + "px";
  heads.style.height = canvas.style.height;
  V.arrangeCanvas(canvas, tracks);
};

V.songBeats = (tracks) => {
  let end = (DAW.single.Song || {}).beats || 0;
  for (const t of tracks) {
    for (const p of DAW.placementsOf(t.path)) {
      const cp = p.comps[A + "ClipPlacement"];
      end = Math.max(end, cp.start + V.placementLength(cp));
    }
  }
  const tr = DAW.single.Transport;
  if (tr && tr.loop) end = Math.max(end, tr.loop_end);
  return Math.max(end, 16);
};

V.clipComp = (clipPath) => {
  const r = DAW.ents[clipPath];
  if (!r) return null;
  return r.comps[A + "NoteClip"] || r.comps[A + "AutomationClip"] || r.comps[A + "AudioClip"];
};

V.placementLength = (cp) => {
  if (cp.duration > 0) return cp.duration;
  const c = V.clipComp(cp.clip);
  return c ? Math.max(0.25, (c.length || 4) - (cp.offset || 0)) : 4;
};

V.fitArrange = () => {
  const lanes = document.querySelector(".lanes");
  if (!lanes) return;
  const beats = V.songBeats(V.trackOrder());
  V.arr.x0 = 0;
  V.arr.ppb = Math.max(2, (lanes.clientWidth - 20) / beats);
};

V.arrangeCanvas = (c, tracks) => {
  const st = V.arr;
  let drag = null;
  const xOf = (b) => (b - st.x0) * st.ppb;
  const bOf = (x) => x / st.ppb + st.x0;
  const rowOf = (y) => Math.floor((y - RULERH) / ROWH);
  const placementAt = (x, y) => {
    const row = rowOf(y);
    const t = tracks[row];
    if (!t || y < RULERH) return null;
    const ps = DAW.placementsOf(t.path);
    for (let i = ps.length - 1; i >= 0; i--) {
      const cp = ps[i].comps[A + "ClipPlacement"];
      const x0 = xOf(cp.start), x1 = xOf(cp.start + V.placementLength(cp));
      if (x >= x0 && x <= x1) return { rec: ps[i], edge: x1 - x < 6 && x1 - x0 > 14, row };
    }
    return null;
  };
  if (!st.fitted) { st.fitted = true; requestAnimationFrame(() => V.fitArrange()); }

  c.draw = () => {
    const parent = c.parentNode;
    if (!parent) return;
    c.style.width = parent.clientWidth + "px";
    const { ctx, w, h } = ED.setupCanvas(c);
    if (!w) return;
    const tr = DAW.single.Transport || { beats_per_bar: 4, beat_unit: 4 };
    const bar = tr.beats_per_bar * 4 / (tr.beat_unit || 4);
    ctx.fillStyle = "#141619";
    ctx.fillRect(0, 0, w, h);
    tracks.forEach((t, i) => {
      ctx.fillStyle = DAW.sel === t.path ? "#1f2328" : i % 2 ? "#171a1d" : "#191c20";
      ctx.fillRect(0, RULERH + i * ROWH, w, ROWH);
    });
    const songEnd = (DAW.single.Song || {}).beats || 0;
    const step = st.ppb * bar < 30 ? bar * 4 : st.ppb < 6 ? bar : 1;
    for (let b = Math.floor(st.x0 / step) * step; xOf(b) < w; b += step) {
      const x = Math.round(xOf(b));
      const isBar = Math.abs((b / bar) - Math.round(b / bar)) < 1e-6;
      ctx.fillStyle = isBar ? "#2a2f35" : "#1e2226";
      ctx.fillRect(x, RULERH, 1, h - RULERH);
    }
    if (songEnd) {
      ctx.fillStyle = "rgba(0,0,0,0.35)";
      const x = xOf(songEnd);
      if (x < w) ctx.fillRect(Math.max(0, x), RULERH, w - x, h);
    }
    tracks.forEach((t, i) => {
      const y = RULERH + i * ROWH;
      for (const p of DAW.placementsOf(t.path)) {
        const cp = p.comps[A + "ClipPlacement"];
        const len = V.placementLength(cp);
        const x0 = xOf(cp.start), x1 = xOf(cp.start + len);
        if (x1 < 0 || x0 > w) continue;
        const selected = DAW.sel === p.path || DAW.sel === cp.clip;
        const clip = DAW.ents[cp.clip];
        const isAuto = clip && DAW.has(clip, "AutomationClip");
        const color = isAuto ? "hsl(200, 60%, 45%)" : DAW.color(t.path);
        ctx.fillStyle = color;
        ctx.globalAlpha = 0.25;
        ctx.fillRect(x0, y + 2, x1 - x0, ROWH - 4);
        ctx.globalAlpha = 1;
        ctx.fillRect(x0, y + 2, x1 - x0, 12);
        ctx.strokeStyle = selected ? "#fff" : "rgba(0,0,0,0.6)";
        ctx.lineWidth = selected ? 2 : 1;
        ctx.strokeRect(x0 + 0.5, y + 2.5, x1 - x0 - 1, ROWH - 5);
        ctx.lineWidth = 1;
        ctx.save();
        ctx.beginPath();
        ctx.rect(x0, y + 2, x1 - x0, ROWH - 4);
        ctx.clip();
        ctx.fillStyle = "#111";
        ctx.font = "10px sans-serif";
        ctx.fillText(DAW.leaf(cp.clip) + (cp.loop ? " ⟳" : "") + (cp.transpose ? ` ${cp.transpose > 0 ? "+" : ""}${cp.transpose}` : ""), x0 + 3, y + 12);
        const c2 = V.clipComp(cp.clip);
        if (c2 && c2.notes && c2.notes.length) {
          const lo = Math.min(...c2.notes.map((n) => n.pitch)), hi = Math.max(...c2.notes.map((n) => n.pitch));
          const span = Math.max(12, hi - lo + 1);
          const nh = Math.max(1.5, (ROWH - 20) / span);
          ctx.fillStyle = "rgba(230,233,237,0.85)";
          const clen = c2.length || 4;
          for (let pass = 0; ; pass++) {
            const base = cp.start - (cp.offset || 0) + pass * clen;
            if (base >= cp.start + len || (!cp.loop && pass > 0)) break;
            for (const n of c2.notes) {
              const s = base + n.start;
              if (s + n.duration <= cp.start || s >= cp.start + len) continue;
              const ny = y + ROWH - 5 - (n.pitch - lo + 1) * ((ROWH - 20) / span);
              ctx.fillRect(xOf(s), ny, Math.max(1, n.duration * st.ppb - 1), nh);
            }
            if (pass > 0) {
              ctx.fillStyle = "rgba(0,0,0,0.4)";
              ctx.fillRect(xOf(base), y + 14, 1, ROWH - 16);
              ctx.fillStyle = "rgba(230,233,237,0.85)";
            }
          }
        }
        if (c2 && c2.curve && c2.curve.keys && c2.curve.keys.length) {
          const keys = c2.curve.keys;
          const lo = Math.min(...keys.map((k) => k.value)), hi = Math.max(...keys.map((k) => k.value));
          const yv = (v) => y + ROWH - 6 - (hi > lo ? (v - lo) / (hi - lo) : 0.5) * (ROWH - 22);
          ctx.strokeStyle = "rgba(230,233,237,0.9)";
          ctx.beginPath();
          const clen = c2.length || 4;
          for (let pass = 0; pass < 512; pass++) {
            const base = cp.start - (cp.offset || 0) + pass * clen;
            if (base >= cp.start + len || (!cp.loop && pass > 0)) break;
            keys.forEach((k, i2) => {
              const x = xOf(base + k.time);
              if (i2 === 0 && pass === 0) ctx.moveTo(xOf(cp.start), yv(k.value));
              ctx.lineTo(x, yv(k.value));
            });
          }
          ctx.lineTo(x1, yv(keys[keys.length - 1].value));
          ctx.stroke();
        }
        ctx.restore();
      }
    });
    ctx.fillStyle = "#23272c";
    ctx.fillRect(0, 0, w, RULERH);
    if (tr.loop && tr.loop_end > tr.loop_start) {
      ctx.fillStyle = "rgba(255,159,46,0.35)";
      ctx.fillRect(xOf(tr.loop_start), 0, (tr.loop_end - tr.loop_start) * st.ppb, RULERH);
      ctx.fillStyle = "rgba(255,159,46,0.05)";
      ctx.fillRect(xOf(tr.loop_start), RULERH, (tr.loop_end - tr.loop_start) * st.ppb, h);
    }
    ctx.fillStyle = "#b4bac2";
    ctx.font = "10px sans-serif";
    const labelStep = Math.max(1, Math.ceil(40 / (st.ppb * bar)));
    for (let b = Math.floor(st.x0 / bar) * bar; xOf(b) < w; b += bar) {
      const n = Math.round(b / bar);
      if (n % labelStep) continue;
      const x = xOf(b);
      ctx.fillRect(x, RULERH - 8, 1, 8);
      ctx.fillText(String(n + 1), x + 3, 12);
    }
    const pos = DAW.position();
    const px = xOf(pos);
    if (st.follow && tr.playing && !drag && (px > w - 40 || px < 0)) st.x0 = Math.max(0, pos - 2 * bar);
    ctx.fillStyle = "#4fc3f7";
    ctx.fillRect(px, 0, 1.5, h);
    ctx.beginPath();
    ctx.moveTo(px - 5, 0); ctx.lineTo(px + 6, 0); ctx.lineTo(px + 0.75, 7);
    ctx.fill();
  };
  ED.canvases.add(c);

  const local = (e) => { const r = c.getBoundingClientRect(); return { x: e.clientX - r.left, y: e.clientY - r.top }; };
  const snap = (b, e) => {
    const s = e.altKey ? 0.25 : st.snap;
    return Math.round(b / s) * s;
  };

  c.addEventListener("pointerdown", (e) => {
    c.focus();
    const { x, y } = local(e);
    c.setPointerCapture(e.pointerId);
    if (y < RULERH) {
      if (e.shiftKey) {
        const b = snap(bOf(x), e);
        drag = { kind: "loop", b0: b };
      } else {
        drag = { kind: "seek" };
        DAW.seek(Math.max(0, bOf(x)));
      }
      DAW.dragging++;
      return;
    }
    const h = placementAt(x, y);
    if (h) {
      DAW.select(h.rec.path);
      const cp = h.rec.comps[A + "ClipPlacement"];
      drag = { kind: h.edge ? "resize" : "move", rec: h.rec, b: bOf(x), start: cp.start, len: V.placementLength(cp) };
      DAW.dragging++;
    } else {
      const t = tracks[rowOf(y)];
      if (t) DAW.select(t.path);
    }
  });
  c.addEventListener("pointermove", (e) => {
    const { x, y } = local(e);
    if (!drag) {
      const h = y >= RULERH ? placementAt(x, y) : null;
      c.style.cursor = y < RULERH ? "pointer" : h ? (h.edge ? "ew-resize" : "grab") : "default";
      return;
    }
    if (drag.kind === "seek") { DAW.seek(Math.max(0, bOf(x))); return; }
    if (drag.kind === "loop") {
      const b = snap(bOf(x), e);
      const lo = Math.max(0, Math.min(drag.b0, b)), hi = Math.max(drag.b0, b);
      if (hi > lo) DAW.transport({ loop: true, loop_start: lo, loop_end: hi });
      return;
    }
    const db = snap(bOf(x) - drag.b, e);
    if (drag.kind === "move") {
      DAW.write(drag.rec.path, A + "ClipPlacement", { start: Math.max(0, drag.start + db) });
    } else {
      DAW.write(drag.rec.path, A + "ClipPlacement", { duration: Math.max(e.altKey ? 0.25 : st.snap, drag.len + db) });
    }
  });
  const end = () => { if (drag) { drag = null; DAW.dragging = Math.max(0, DAW.dragging - 1); } };
  c.addEventListener("dblclick", (e) => {
    const { x, y } = local(e);
    if (y < RULERH) return;
    const h = placementAt(x, y);
    const t = tracks[rowOf(y)];
    DAW.openWindow(h ? h.rec.path : t ? t.path : null);
  });
  c.addEventListener("pointerup", end);
  c.addEventListener("pointercancel", end);
  c.addEventListener("wheel", (e) => {
    e.preventDefault();
    if (e.ctrlKey || e.metaKey) {
      const { x } = local(e);
      const b = bOf(x);
      st.ppb = Math.max(1, Math.min(400, st.ppb * Math.exp(-e.deltaY * 0.01)));
      st.x0 = Math.max(0, b - x / st.ppb);
    } else if (e.shiftKey || Math.abs(e.deltaX) > Math.abs(e.deltaY)) {
      st.x0 = Math.max(0, st.x0 + (e.deltaX || e.deltaY) / st.ppb);
    } else {
      c.closest(".arrange-body").scrollTop += e.deltaY;
    }
  }, { passive: false });
  c.addEventListener("keydown", (e) => {
    const rec = DAW.ents[DAW.sel];
    if (!rec || !DAW.has(rec, "ClipPlacement")) return;
    if (e.key === "Delete" || e.key === "Backspace") {
      DAW.del(rec.path).then(() => DAW.toast(`deleted ${rec.path}`), (err) => DAW.toast(String(err), true));
      DAW.select(rec.parent);
      e.preventDefault();
      e.stopPropagation();
    } else if (e.key === "d" && (e.metaKey || e.ctrlKey)) {
      e.preventDefault();
      e.stopPropagation();
      V.duplicatePlacement(rec);
    }
  });
};

V.duplicatePlacement = async (rec) => {
  const cp = DAW.clone(rec.comps[A + "ClipPlacement"]);
  const path = DAW.uniqueName(rec.parent, rec.name.replace(/_\d+$/, ""));
  cp.start = cp.start + V.placementLength(cp);
  try {
    await DAW.create(path);
    await DAW.request("PUT", "component/" + DAW.entityPath(path), {
      component: A + "ClipPlacement", value: encodeURIComponent(JSON.stringify(cp))
    });
    DAW.select(path);
  } catch (err) {
    DAW.toast(String(err), true);
  }
};

V.addEffectMenu = (e, bus, slot) => {
  const items = EFFECTS.map((fx) => ({ label: fx, action: () => V.addEffect(bus, slot, fx) }));
  const loose = DAW.effects().filter((r) => !DAW.busOfEffect(r.path));
  if (loose.length) {
    items.push(null);
    for (const r of loose) items.push({ label: `use ${r.path}`, action: () => V.setSlot(bus, slot, r.path) });
  }
  UI.menu(e.clientX, e.clientY, items);
};

V.setSlot = (bus, slot, fx) => {
  const b = DAW.val(bus, "AudioBus");
  const effects = (b.effects || ["#0", "#0", "#0", "#0"]).slice();
  effects[slot] = fx || "#0";
  DAW.write(bus, A + "AudioBus", { effects });
  DAW.sig = "";
};

V.addEffect = async (bus, slot, fx) => {
  const path = DAW.uniqueName(bus, fx.toLowerCase());
  try {
    await DAW.create(path);
    await DAW.add(path, A + fx);
    V.setSlot(bus, slot, path);
    DAW.select(path);
    DAW.toast(`added ${fx} to ${bus}`);
  } catch (err) {
    DAW.toast(String(err), true);
  }
};

V.effectMenu = (e, bus, slot, fx) => {
  const rec = DAW.ents[fx];
  const b = DAW.val(bus, "AudioBus");
  const items = [
    { label: "Open", action: () => DAW.openWindow(fx) },
    rec && rec.path !== bus ? { label: rec.disabled ? "Enable" : "Bypass (disable entity)", action: () => DAW.enable(fx, rec.disabled) } : null,
    slot > 0 ? { label: "Move left", action: () => V.moveSlot(bus, slot, -1) } : null,
    slot < 3 ? { label: "Move right", action: () => V.moveSlot(bus, slot, 1) } : null,
    { label: "Remove from slot", action: () => V.setSlot(bus, slot, null) }
  ].filter(Boolean);
  if (rec && rec.path.startsWith(bus + ".") && b) {
    items.push({ label: "Delete effect entity", action: () => { V.setSlot(bus, slot, null); DAW.del(fx); } });
  }
  UI.menu(e.clientX, e.clientY, items);
};

V.moveSlot = (bus, slot, d) => {
  const b = DAW.val(bus, "AudioBus");
  const effects = b.effects.slice();
  const j = slot + d;
  [effects[slot], effects[j]] = [effects[j], effects[slot]];
  DAW.write(bus, A + "AudioBus", { effects });
  DAW.sig = "";
};

V.strip = (rec) => {
  const path = rec.path;
  const bus = rec.comps[A + "AudioBus"];
  const isMaster = path === A + "master";
  const isTrack = DAW.has(rec, "Track");
  const name = isMaster ? "Master" : isTrack ? V.laneName(path) : rec.name;
  const kind = isMaster ? "master" : isTrack ? "track" : UI.enumLabel(bus.kind).toLowerCase();
  const el = UI.el("div", { class: "strip" + (DAW.sel === path ? " sel" : "") + (rec.disabled ? " off" : ""), "data-path": path });
  const ins = UI.el("div", { class: "inserts" });
  (bus.effects || []).forEach((fx, i) => {
    if (DAW.isNone(fx)) {
      ins.appendChild(UI.el("div", { class: "ins empty", title: "Add an effect", onclick: (e) => V.addEffectMenu(e, path, i) }, "+"));
    } else {
      const frec = DAW.ents[fx];
      const label = frec ? DAW.effectComps(frec).map(DAW.short).join("/") || DAW.leaf(fx) : DAW.leaf(fx);
      ins.appendChild(UI.el("div", {
        class: "ins" + (frec && frec.disabled ? " off" : ""), title: `${fx}\nclick to open in its own window, right click for options`,
        style: { borderLeft: `3px solid ${DAW.color(fx)}` },
        onclick: () => DAW.openWindow(fx),
        oncontextmenu: (e) => { e.preventDefault(); V.effectMenu(e, path, i, fx); }
      }, label));
    }
  });
  el.appendChild(ins);
  if (!isMaster) {
    const sends = UI.el("div", { class: "sends" });
    (bus.sends || []).forEach((s, i) => {
      const range = UI.el("input", { type: "range", min: 0, max: 1, step: 0.01, value: Math.min(1, s.level), title: `send to ${s.bus}` });
      range.addEventListener("input", () => {
        const list = DAW.clone(DAW.val(path, "AudioBus").sends);
        list[i].level = parseFloat(range.value);
        DAW.write(path, A + "AudioBus", { sends: list });
      });
      range.addEventListener("pointerdown", () => range.classList.add("active"));
      range.addEventListener("pointerup", () => range.classList.remove("active"));
      UI.bind(path, A + "AudioBus", ["sends", i, "level"], (v) => { range.value = Math.min(1, v); }, range);
      sends.appendChild(UI.el("div", { class: "send" }, UI.el("span", { title: s.bus }, (s.pre_fader ? "pre " : "") + DAW.leaf(s.bus)), range));
    });
    const others = DAW.buses().filter((b) => b.path !== path && b.path !== A + "master" && !(bus.sends || []).some((s) => s.bus === b.path));
    if ((bus.sends || []).length < 4 && others.length) {
      sends.appendChild(UI.el("button", {
        class: "small", text: "+ send", onclick: (e) => UI.menu(e.clientX, e.clientY, others.map((o) => ({
          label: o.path, action: () => {
            const list = DAW.clone(bus.sends || []);
            list.push({ bus: o.path, level: 0.5, pre_fader: false });
            DAW.write(path, A + "AudioBus", { sends: list });
            DAW.sig = "";
          }
        })))
      }));
    }
    el.appendChild(sends);
  } else {
    el.appendChild(UI.el("div", { class: "sends" }));
  }
  const pan = UI.knob({ label: "pan", meta: UI.meta("AudioBus", "pan"), value: bus.pan, onchange: (v) => DAW.write(path, A + "AudioBus", { pan: v }) });
  const width = UI.knob({ label: "width", meta: UI.meta("AudioBus", "width"), value: bus.width, onchange: (v) => DAW.write(path, A + "AudioBus", { width: v }) });
  UI.bind(path, A + "AudioBus", ["pan"], (v) => pan.set(v), pan);
  UI.bind(path, A + "AudioBus", ["width"], (v) => width.set(v), width);
  el.appendChild(UI.el("div", { class: "knobs" }, pan, width));
  if (!isMaster) {
    const out = UI.select({
      options: [{ value: A + "master", label: "→ master" }].concat(DAW.buses().filter((b) => b.path !== path && b.path !== A + "master")
        .map((b) => ({ value: b.path, label: "→ " + b.path }))),
      value: DAW.isNone(bus.output) ? A + "master" : bus.output,
      onchange: (v) => DAW.write(path, A + "AudioBus", { output: v })
    });
    el.appendChild(UI.el("div", { class: "out" }, out));
  } else {
    el.appendChild(UI.el("div", { class: "out" }));
  }
  const db = UI.el("div", { class: "db" });
  const fader = UI.fader({
    value: bus.volume, onchange: (v) => DAW.write(path, A + "AudioBus", { volume: v }),
    onlabel: (v) => { db.textContent = UI.fmt(v, { unit: "gain" }); }
  });
  UI.bind(path, A + "AudioBus", ["volume"], (v) => fader.set(v), fader);
  const meter = UI.meter(path);
  el.appendChild(UI.el("div", { class: "fader-row" }, fader, meter));
  el.appendChild(db);
  const mute = UI.toggle({ label: "M", cls: "small mute", value: bus.mute, onchange: (v) => DAW.write(path, A + "AudioBus", { mute: v }) });
  UI.bind(path, A + "AudioBus", ["mute"], (v) => mute.set(v), mute);
  const btns = UI.el("div", { class: "btns" }, mute);
  if (!isMaster) {
    const solo = UI.toggle({ label: "S", cls: "small solo", value: bus.solo, onchange: (v) => DAW.write(path, A + "AudioBus", { solo: v }) });
    UI.bind(path, A + "AudioBus", ["solo"], (v) => solo.set(v), solo);
    btns.appendChild(solo);
  }
  el.appendChild(btns);
  el.appendChild(UI.el("div", { class: "skind" }, kind));
  el.appendChild(UI.el("div", {
    class: "sname", title: path + "\nclick to open in its own window", style: { borderTopColor: DAW.color(path) },
    onclick: () => DAW.openWindow(path)
  }, name));
  return el;
};

V.mixer = (root) => {
  const el = UI.el("div", { class: "mixer" });
  const buses = DAW.buses();
  const tracks = V.trackOrder();
  const rank = { AudioChannelGroup: 1, AudioChannelReturn: 2, AudioChannelSfx: 3 };
  const rest = buses.filter((b) => !DAW.has(b, "Track") && b.path !== A + "master")
    .sort((a, b) => rank[a.comps[A + "AudioBus"].kind] - rank[b.comps[A + "AudioBus"].kind] || a.order - b.order);
  for (const r of tracks.concat(rest)) el.appendChild(V.strip(r));
  const master = DAW.ents[A + "master"];
  if (master) el.appendChild(V.strip(master));
  root.appendChild(el);
};

V.modulation = (root) => {
  const el = UI.el("div", { class: "macro-grid" });
  const macros = DAW.list("AudioMacro");
  el.appendChild(UI.el("div", { class: "section-title" }, "Macros"));
  if (!macros.length) el.appendChild(UI.el("div", { style: { color: "var(--fg2)" } }, "No macros"));
  for (const m of macros) {
    const v = m.comps[A + "AudioMacro"];
    const k = UI.knob({ label: m.name, meta: { min: 0, max: 1, def: 0 }, value: v.value, onchange: (x) => DAW.write(m.path, A + "AudioMacro", { value: x }) });
    UI.bind(m.path, A + "AudioMacro", ["value"], (x) => k.set(x), k);
    const maps = DAW.childrenOf(m.path).filter((r) => DAW.has(r, "AudioMacroMap")).map((r) => {
      const mm = r.comps[A + "AudioMacroMap"];
      return UI.el("div", { title: `${mm.target} ${DAW.short(mm.component)}.${mm.member}` },
        `${DAW.leaf(mm.target)}.${mm.member} ${UI.fmt(mm.min, {})}→${UI.fmt(mm.max, {})}`);
    });
    el.appendChild(UI.el("div", { class: "macro" + (DAW.sel === m.path ? " sel" : ""), "data-path": m.path, onclick: (e) => { if (e.target === e.currentTarget) DAW.openWindow(m.path); } },
      k, UI.el("a", { class: "link", onclick: () => DAW.openWindow(m.path) }, "edit maps"), UI.el("div", { class: "maps" }, maps)));
  }
  el.appendChild(UI.el("div", { class: "section-title" }, "Modulators"));
  const mods = DAW.list("AudioModulator");
  if (!mods.length) el.appendChild(UI.el("div", { style: { color: "var(--fg2)" } }, "No modulators"));
  for (const m of mods) {
    const mv = m.comps[A + "AudioModulator"];
    el.appendChild(V.cardFor(m.path, A + "AudioModulator", {
      title: m.name, sel: DAW.sel === m.path,
      buttons: [UI.el("span", { class: "comp" }, `${DAW.leaf(mv.target)} ${DAW.short(mv.component)}.${mv.member}`), V.enableBtn(m)]
    }));
  }
  root.appendChild(el);
};

V.enableBtn = (rec) => UI.el("button", {
  class: "small" + (rec.disabled ? "" : " on"), title: rec.disabled ? "Enable" : "Disable",
  text: rec.disabled ? "off" : "on",
  onclick: (e) => { e.stopPropagation(); DAW.enable(rec.path, rec.disabled).then(() => { rec.disabled = !rec.disabled; DAW.sig = ""; }); }
});

V.cardFor = (path, comp, opts) => {
  opts = Object.assign({}, opts);
  const s = DAW.short(comp);
  if (s === "Equalizer" || s === "AudioFilter") opts.graph = ED.responseGraph(path, comp);
  if (/Envelope$/.test(s) && s !== "SynthPitchEnvelope") opts.graph = ED.envelopeGraph(path, comp);
  return UI.card(path, comp, opts);
};

V.effectCards = (fxPath, opts) => {
  const rec = DAW.ents[fxPath];
  if (!rec) return [UI.el("div", { class: "slot-empty" }, `missing ${fxPath}`)];
  return DAW.effectComps(rec).map((comp) => {
    const gr = UI.el("span", { class: "gr", title: "Gain reduction" });
    V.grs.push({ el: gr, path: fxPath });
    const buttons = [gr].concat(opts.buttons || []);
    return V.cardFor(fxPath, comp, { title: DAW.leaf(fxPath), buttons, sel: DAW.sel === fxPath });
  });
};

V.grs = [];

V.updateGr = () => {
  V.grs = V.grs.filter((g) => g.el.isConnected);
  for (const g of V.grs) {
    const m = DAW.meters[g.path];
    g.el.textContent = m && m.reduction > 0.05 ? `-${m.reduction.toFixed(1)}dB` : "";
  }
};

V.slot = (busPath, i) => {
  const fx = DAW.val(busPath, "AudioBus").effects[i];
  if (DAW.isNone(fx)) {
    return [UI.el("div", { class: "slot-empty" }, `slot ${i + 1}`,
      UI.el("button", { class: "small", text: "+ effect", onclick: (e) => V.addEffectMenu(e, busPath, i) }))];
  }
  const rec = DAW.ents[fx];
  const buttons = [
    rec && fx !== busPath ? UI.el("button", {
      class: "small" + (rec.disabled ? "" : " on"), text: rec.disabled ? "off" : "on", title: "Bypass by disabling the effect entity",
      onclick: () => DAW.enable(fx, rec.disabled).then(() => { rec.disabled = !rec.disabled; DAW.sig = ""; })
    }) : null,
    i > 0 ? UI.el("button", { class: "small", text: "◀", title: "Move left", onclick: () => V.moveSlot(busPath, i, -1) }) : null,
    i < 3 ? UI.el("button", { class: "small", text: "▶", title: "Move right", onclick: () => V.moveSlot(busPath, i, 1) }) : null,
    UI.el("button", { class: "small", text: "✕", title: "Remove from slot", onclick: () => V.setSlot(busPath, i, null) })
  ].filter(Boolean);
  return V.effectCards(fx, { buttons });
};

V.chain = (busPath) => {
  const bus = DAW.val(busPath, "AudioBus");
  const chain = UI.el("div", { class: "chain" });
  (bus.effects || []).forEach((_, i) => V.slot(busPath, i).forEach((c) => chain.appendChild(c)));
  return chain;
};

V.synthEditor = (soundPath) => {
  const rec = DAW.ents[soundPath];
  const wrap = UI.el("div", { class: "row", style: { flexDirection: "column", alignItems: "stretch" } });
  if (!rec) return wrap;
  const cards = UI.el("div", { class: "chain" });
  for (const comp of Object.keys(rec.comps)) {
    if (DAW.hidden(comp) || rec.comps[comp] === null || comp === A + "AudioPcm") continue;
    cards.appendChild(V.cardFor(soundPath, comp, { title: comp === A + "SynthSound" ? rec.name : null }));
  }
  wrap.appendChild(UI.el("div", { class: "section-title" }, "Sound " + soundPath));
  wrap.appendChild(cards);
  for (const child of DAW.childrenOf(soundPath)) {
    const layer = UI.el("div", { class: "chain" });
    for (const comp of Object.keys(child.comps)) {
      if (DAW.hidden(comp) || child.comps[comp] === null) continue;
      layer.appendChild(V.cardFor(child.path, comp, {}));
    }
    wrap.appendChild(UI.el("div", { class: "section-title" }, "Layer " + child.name));
    wrap.appendChild(layer);
    for (const sub of DAW.childrenOf(child.path)) {
      const row = UI.el("div", { class: "chain" });
      for (const comp of Object.keys(sub.comps)) {
        if (DAW.hidden(comp) || sub.comps[comp] === null) continue;
        row.appendChild(V.cardFor(sub.path, comp, {}));
      }
      wrap.appendChild(UI.el("div", { class: "section-title" }, `${child.name} / ${sub.name}`));
      wrap.appendChild(row);
    }
  }
  return wrap;
};

V.genericCards = (path, skip) => {
  const rec = DAW.ents[path];
  const row = UI.el("div", { class: "chain" });
  if (!rec) return row;
  for (const comp of Object.keys(rec.comps)) {
    if (DAW.hidden(comp) || rec.comps[comp] === null || (skip && skip.includes(comp))) continue;
    row.appendChild(V.cardFor(path, comp, {}));
  }
  return row;
};

V.instrumentBlock = (instPath, body, auditionPath) => {
  const inst = DAW.val(instPath, "Instrument");
  if (!inst) return;
  body.appendChild(UI.el("div", { class: "section-title" }, "Instrument ", UI.el("a", { class: "link", onclick: () => DAW.select(instPath) }, instPath)));
  body.appendChild(UI.el("div", { class: "chain" }, V.cardFor(instPath, A + "Instrument", { title: DAW.leaf(instPath) })));
  if (!DAW.isNone(inst.sound)) body.appendChild(V.synthEditor(inst.sound));
  body.appendChild(ED.keyboard(() => V.auditionFor(auditionPath || instPath)));
};

V.clipEditor = (clipPath, body, placementPath) => {
  const rec = DAW.ents[clipPath];
  if (!rec) return;
  const audition = () => V.auditionFor(placementPath || clipPath);
  if (DAW.has(rec, "NoteClip")) {
    const head = UI.el("div", { class: "piano-tools" }, UI.el("b", {}, "Notes "), UI.el("a", { class: "link", onclick: () => DAW.select(clipPath) }, clipPath));
    const roll = ED.pianoRoll(clipPath, { placement: placementPath, audition, height: 300 });
    const state = V.rollState[clipPath];
    if (state) Object.assign(roll.view, state, { fitted: true });
    V.rollState[clipPath] = roll.view;
    head.appendChild(UI.el("label", {}, "snap", UI.select({
      options: [{ value: "1", label: "1/4" }, { value: "0.5", label: "1/8" }, { value: "0.25", label: "1/16" }, { value: "0.125", label: "1/32" }],
      value: String(roll.view.snap), onchange: (v) => { roll.view.snap = parseFloat(v); }
    })));
    const len = UI.dragNum({
      value: rec.comps[A + "NoteClip"].length, meta: { min: 0.25 }, step: 0.25,
      format: (v) => `${v} beats`, onchange: (v) => DAW.write(clipPath, A + "NoteClip", { length: v })
    });
    UI.bind(clipPath, A + "NoteClip", ["length"], (v) => len.set(v), len);
    head.appendChild(UI.el("label", {}, "length", len));
    head.appendChild(UI.el("button", { class: "small", text: "fit", onclick: () => roll.fit() }));
    head.appendChild(UI.el("span", { style: { color: "var(--fg2)" } },
      "double click adds, drag moves, edge resizes, alt+drag copies, shift selects, arrows nudge, Delete removes, drag in the lower lane sets velocity"));
    body.appendChild(UI.el("div", { class: "piano-wrap" }, head, roll));
    const a = audition();
    if (a) body.appendChild(ED.keyboard(audition));
  } else if (DAW.has(rec, "AutomationClip")) {
    const ac = rec.comps[A + "AutomationClip"];
    body.appendChild(UI.el("div", { class: "piano-tools" }, UI.el("b", {}, "Automation "),
      UI.el("a", { class: "link", onclick: () => DAW.select(clipPath) }, clipPath),
      UI.el("span", {}, ` → ${ac.target} ${DAW.short(ac.component)}.${ac.member}`),
      UI.el("span", { style: { color: "var(--fg2)" } }, "  double click adds a key, drag moves, Delete removes")));
    body.appendChild(ED.curveEditor(clipPath, { placement: placementPath }));
    body.appendChild(UI.el("div", { class: "chain", style: { marginTop: "8px" } }, V.cardFor(clipPath, A + "AutomationClip", { skip: ["curve"] })));
  } else {
    body.appendChild(UI.el("div", { class: "chain" }, V.cardFor(clipPath, A + "AudioClip", { title: DAW.leaf(clipPath) })));
  }
};

V.rollState = {};

V.renderDetail = () => {
  const el = document.getElementById("detail");
  const scroll = el.scrollTop;
  el.textContent = "";
  ED.activeKeyboard = null;
  const rec = DAW.ents[DAW.sel];
  if (!rec) {
    el.appendChild(UI.el("div", { class: "detail-empty" }, !DAW.conn ? "" : !DAW.connected ? `Connecting to ${DAW.host}...` :
      `${DAW.sel || "no entity"} is not in ${DAW.host}, waiting for it to appear`));
    return;
  }
  const kind = DAW.kindOf(rec);
  const head = UI.el("div", { class: "detail-head" },
    UI.el("span", { class: "swatch", style: { background: DAW.color(rec.path) } }),
    UI.el("span", { class: "title" }, kind === "track" ? V.laneName(rec.path) : rec.name),
    UI.el("span", { class: "kind" }, `${kind}  ${rec.path}${rec.prefab ? "  (prefab)" : ""}${rec.script ? "  from " + rec.script : ""}`),
    UI.el("span", { class: "grow" }));
  if (kind !== "track" && kind !== "bus" && rec.path !== A + "master") head.appendChild(V.enableBtn(rec));
  if (!DAW.detailPath) {
    head.appendChild(UI.el("button", { class: "small", text: "window", title: "Open in its own window", onclick: () => DAW.openWindow(rec.path) }));
    head.appendChild(UI.el("button", { class: "small", text: "×", title: "Close (Escape)", onclick: () => G.clearFocus() }));
  }
  if (kind === "placement") {
    head.appendChild(UI.el("button", { class: "small", text: "duplicate", onclick: () => V.duplicatePlacement(rec) }));
    head.appendChild(UI.el("button", {
      class: "small", text: "delete", onclick: () => { DAW.del(rec.path); DAW.select(rec.parent); }
    }));
  }
  el.appendChild(head);
  const body = UI.el("div", { class: "detail-body" });
  el.appendChild(body);
  if (kind === "track" || kind === "bus") {
    body.appendChild(UI.el("div", { class: "section-title" }, "Inserts"));
    body.appendChild(V.chain(rec.path));
    body.appendChild(UI.el("div", { class: "section-title" }, "Channel"));
    const row = UI.el("div", { class: "chain" }, V.cardFor(rec.path, A + "AudioBus", { skip: ["effects"] }));
    if (kind === "track") row.appendChild(V.cardFor(rec.path, A + "Track", {}));
    for (const comp of Object.keys(rec.comps)) {
      if (DAW.hidden(comp) || rec.comps[comp] === null || DAW.effectComp(comp)) continue;
      if ([A + "AudioBus", A + "Track", A + "AudioMeter"].includes(comp)) continue;
      row.appendChild(V.cardFor(rec.path, comp, {}));
    }
    body.appendChild(row);
    if (kind === "track") {
      const t = rec.comps[A + "Track"];
      const ps = DAW.placementsOf(rec.path);
      if (ps.length) {
        body.appendChild(UI.el("div", { class: "section-title" }, "Clips"));
        body.appendChild(UI.el("div", { class: "row" }, ps.map((p) => UI.el("a", {
          class: "link", onclick: () => DAW.select(p.path)
        }, `${p.name} (${DAW.leaf(p.comps[A + "ClipPlacement"].clip)})`))));
      }
      if (!DAW.isNone(t.instrument)) V.instrumentBlock(t.instrument, body, rec.path);
    }
  } else if (kind === "effect") {
    const bus = DAW.busOfEffect(rec.path);
    if (bus) {
      const i = bus.comps[A + "AudioBus"].effects.indexOf(rec.path);
      body.appendChild(UI.el("div", { class: "section-title" }, `Insert slot ${i + 1} of `, UI.el("a", { class: "link", onclick: () => DAW.select(bus.path) }, bus.path)));
      body.appendChild(UI.el("div", { class: "chain" }, V.slot(bus.path, i)));
    } else {
      body.appendChild(UI.el("div", { class: "section-title" }, "Not in an insert slot"));
      body.appendChild(UI.el("div", { class: "chain" }, V.effectCards(rec.path, {})));
    }
    const other = V.genericCards(rec.path, DAW.effectComps(rec).concat([A + "AudioBus", A + "AudioMeter"]));
    if (other.childNodes.length) body.appendChild(other);
  } else if (kind === "placement") {
    const cp = rec.comps[A + "ClipPlacement"];
    body.appendChild(UI.el("div", { class: "chain" }, V.cardFor(rec.path, A + "ClipPlacement", {})));
    V.clipEditor(cp.clip, body, rec.path);
  } else if (kind === "clip") {
    V.clipEditor(rec.path, body, null);
    if (DAW.has(rec, "NoteClip")) body.appendChild(UI.el("div", { class: "chain", style: { marginTop: "8px" } }, V.cardFor(rec.path, A + "NoteClip", { skip: ["notes"] })));
  } else if (kind === "instrument") {
    V.instrumentBlock(rec.path, body, rec.path);
  } else if (kind === "sound") {
    body.appendChild(V.synthEditor(rec.path));
    body.appendChild(ED.keyboard(() => V.auditionFor(rec.path)));
  } else if (kind === "layer") {
    body.appendChild(V.synthEditor(rec.parent));
    body.appendChild(ED.keyboard(() => V.auditionFor(rec.parent)));
  } else if (kind === "macro") {
    body.appendChild(V.genericCards(rec.path));
    const maps = DAW.childrenOf(rec.path).filter((r) => DAW.has(r, "AudioMacroMap"));
    if (maps.length) {
      body.appendChild(UI.el("div", { class: "section-title" }, "Maps"));
      body.appendChild(UI.el("div", { class: "chain" }, maps.map((m) => V.cardFor(m.path, A + "AudioMacroMap", { title: m.name }))));
    }
  } else {
    body.appendChild(V.genericCards(rec.path));
    if (DAW.has(rec, "AudioSource")) {
      const s = rec.comps[A + "AudioSource"];
      if (!DAW.isNone(s.sound)) body.appendChild(UI.el("a", { class: "link", onclick: () => DAW.select(s.sound) }, `sound ${s.sound}`));
    }
  }
  el.scrollTop = scroll;
};

V.prune = () => {
  DAW.bindings = DAW.bindings.filter((b) => b.el && b.el.isConnected);
};

V.markSel = () => {
  document.querySelectorAll("[data-path]").forEach((el) => el.classList.toggle("sel", el.dataset.path === DAW.sel));
};

V.sideOpen = () => !DAW.detailPath && DAW.view === "graph" && !!DAW.sel && !!DAW.ents[DAW.sel];

V.updateSide = () => {
  const open = V.sideOpen();
  const was = document.body.classList.contains("side-open");
  document.body.classList.toggle("side-open", open);
  if (open) V.renderDetail();
  else if (was) document.getElementById("detail").textContent = "";
};

V.renderSelection = () => {
  if (DAW.detailPath) V.renderDetail();
  else V.updateSide();
  V.markSel();
  V.prune();
};

V.renderAll = () => {
  DAW.bindings = [];
  if (DAW.detailPath) V.renderDetail();
  else {
    V.renderView();
    V.updateSide();
  }
  V.prune();
  DAW.needRender = false;
};
