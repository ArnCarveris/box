"use strict";

const UI = window.UI = {};

const OVERRIDES = {
  "Bus.volume": { min: 0, max: 4, def: 1, unit: "gain" },
  "Bus.pan": { min: -1, max: 1, def: 0 },
  "Bus.width": { min: 0, max: 2, def: 1 },
  "Send.level": { min: 0, max: 4, def: 0.5, unit: "gain" },
  "Filter.resonance": { def: 0 },
  "SynthFilter.resonance": { def: 0 },
  "Source.volume": { min: 0, max: 4, def: 1, unit: "gain" },
  "Source.pitch": { min: 0.125, max: 8, def: 1, log: true },
  "Stream.volume": { min: 0, max: 4, def: 1, unit: "gain" },
  "Settings.master_volume": { min: 0, max: 4, def: 1, unit: "gain" },
  "ClipPlacement.gain": { min: 0, max: 4, def: 1, unit: "gain" },
  "ClipPlacement.transpose": { min: -24, max: 24, def: 0, unit: "st" },
  "ClipPlacement.start": { min: 0, max: 256, def: 0, unit: "beats", soft: true },
  "ClipPlacement.duration": { min: 0, max: 256, def: 0, unit: "beats", soft: true },
  "ClipPlacement.offset": { min: 0, max: 64, def: 0, unit: "beats", soft: true },
  "Instrument.root": { min: 0, max: 127, def: 69, unit: "note" },
  "Instrument.velocity_curve": { min: 0, max: 4, def: 2 },
  "Modulator.depth": { min: -1, max: 1, def: 0, soft: true },
  "MacroMap.min": { soft: true, min: 0, max: 1 },
  "MacroMap.max": { soft: true, min: 0, max: 1 },
  "MacroMap.curve": { min: -1, max: 1, def: 0 },
  "Transport.position": { min: 0, max: 256, soft: true, unit: "beats" },
  "Transport.loop_start": { min: 0, max: 256, soft: true, unit: "beats" },
  "Transport.loop_end": { min: 0, max: 256, soft: true, unit: "beats" },
  "SynthOscillator.level": { min: 0, max: 2, def: 1, unit: "gain" },
  "NoteClip.length": { min: 1, max: 128, def: 4, unit: "beats", soft: true },
  "AutomationClip.length": { min: 1, max: 128, def: 4, unit: "beats", soft: true },
  "Clip.length": { min: 1, max: 128, def: 4, unit: "beats", soft: true }
};

const NESTED = {
  "Equalizer.bands": "EqBand",
  "Bus.sends": "Send",
  "Track.humanize": "TrackHumanize",
  "Song.lanes": "SongLane",
  "NoteClip.notes": "Note"
};

UI.meta = (struct, member) => {
  const base = (window.BOX_AUDIO_PARAMS[struct] || {})[member] || {};
  const over = OVERRIDES[struct + "." + member] || {};
  const m = Object.assign({ min: 0, max: 1, unit: "" }, base, over);
  if (m.log && m.min <= 0) m.min = m.max / 2000;
  return m;
};

UI.doc = (struct, member) => {
  const base = (window.BOX_AUDIO_PARAMS[struct] || {})[member];
  return base ? base.doc : "";
};

UI.el = (tag, attrs, ...children) => {
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

UI.bind = (path, comp, keys, update, el) => {
  DAW.bindings.push({ path, comp, keys, update, el });
};

UI.dig = (obj, keys) => {
  let v = obj;
  for (const k of keys) {
    if (v === undefined || v === null) return undefined;
    v = v[k];
  }
  return v;
};

UI.refresh = () => {
  for (const b of DAW.bindings) {
    if (b.el && (b.el.classList.contains("active") || b.el.contains(document.activeElement))) continue;
    if (DAW.isPending(b.path, b.comp)) continue;
    let base;
    if (b.path === A + DAW.short(b.comp) && DAW.single[DAW.short(b.comp)]) {
      base = DAW.single[DAW.short(b.comp)];
    } else {
      const rec = DAW.ents[b.path];
      base = rec ? rec.comps[b.comp] : undefined;
    }
    if (base === undefined) continue;
    b.update(UI.dig(base, b.keys));
  }
};

UI.fmt = (v, m) => {
  if (v === undefined || v === null || Number.isNaN(v)) return "-";
  const u = m.unit;
  const n = (x, d) => {
    const s = x.toFixed(d);
    return s === "-0" || /^-0\.0+$/.test(s) ? s.slice(1) : s;
  };
  if (m.zero && v === 0) return "off";
  if (u === "Hz") return Math.abs(v) >= 1000 ? n(v / 1000, 2) + "k" : n(v, v < 10 ? 2 : 0);
  if (u === "s") return Math.abs(v) < 1 ? n(v * 1000, v < 0.01 ? 1 : 0) + "ms" : n(v, 2) + "s";
  if (u === "ms") return n(v, 1) + "ms";
  if (u === "dB") return n(v, 1) + "dB";
  if (u === "gain") return v <= 0.00001 ? "-inf" : n(20 * Math.log10(v), 1) + "dB";
  if (u === "st") return (v > 0 ? "+" : "") + n(v, Number.isInteger(v) ? 0 : 1) + "st";
  if (u === "ct") return n(v, 0) + "ct";
  if (u === "beats") return n(v, Number.isInteger(v) ? 0 : 2);
  if (u === "oct") return n(v, 2) + "oct";
  if (u === "note") return UI.noteName(v);
  if (m.int) return String(Math.round(v));
  const a = Math.abs(v);
  return n(v, a >= 100 ? 0 : a >= 10 ? 1 : 2);
};

UI.noteName = (p) => {
  const names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
  const r = Math.round(p);
  return names[((r % 12) + 12) % 12] + (Math.floor(r / 12) - 1);
};

UI.toNorm = (v, m) => {
  const lo = m.min, hi = Math.max(m.max, m.min + 1e-9);
  if (m.log) {
    if (v <= 0) return 0;
    return Math.max(0, Math.min(1, Math.log(v / lo) / Math.log(hi / lo)));
  }
  return Math.max(0, Math.min(1, (v - lo) / (hi - lo)));
};

UI.fromNorm = (n, m) => {
  n = Math.max(0, Math.min(1, n));
  let v;
  if (m.log) {
    if (m.zero && n <= 0.002) return 0;
    v = m.min * Math.pow(m.max / m.min, n);
  } else {
    v = m.min + n * (m.max - m.min);
  }
  if (m.int) v = Math.round(v);
  return v;
};

function arcPath(cx, cy, r, a0, a1) {
  const x0 = cx + r * Math.cos(a0), y0 = cy + r * Math.sin(a0);
  const x1 = cx + r * Math.cos(a1), y1 = cy + r * Math.sin(a1);
  const large = Math.abs(a1 - a0) > Math.PI ? 1 : 0;
  const sweep = a1 > a0 ? 1 : 0;
  return `M${x0.toFixed(2)},${y0.toFixed(2)} A${r},${r} 0 ${large} ${sweep} ${x1.toFixed(2)},${y1.toFixed(2)}`;
}

UI.knob = (opts) => {
  const m = Object.assign({}, opts.meta);
  let value = opts.value;
  const svgNS = "http://www.w3.org/2000/svg";
  const svg = document.createElementNS(svgNS, "svg");
  svg.setAttribute("viewBox", "0 0 36 36");
  const bg = document.createElementNS(svgNS, "path");
  bg.setAttribute("fill", "none");
  bg.setAttribute("stroke", "#0c0e10");
  bg.setAttribute("stroke-width", "4");
  bg.setAttribute("stroke-linecap", "round");
  const arc = document.createElementNS(svgNS, "path");
  arc.setAttribute("class", "arc-val");
  arc.setAttribute("fill", "none");
  arc.setAttribute("stroke", opts.color || "#ff9f2e");
  arc.setAttribute("stroke-width", "4");
  arc.setAttribute("stroke-linecap", "round");
  const ptr = document.createElementNS(svgNS, "line");
  ptr.setAttribute("stroke", "#e6e9ed");
  ptr.setAttribute("stroke-width", "2");
  ptr.setAttribute("stroke-linecap", "round");
  const dot = document.createElementNS(svgNS, "circle");
  dot.setAttribute("cx", "18");
  dot.setAttribute("cy", "18");
  dot.setAttribute("r", "10");
  dot.setAttribute("fill", "#2c3137");
  svg.append(bg, arc, dot, ptr);
  const A0 = Math.PI * 0.75, A1 = Math.PI * 2.25;
  bg.setAttribute("d", arcPath(18, 18, 14, A0, A1));
  const label = UI.el("div", { class: "klabel", text: opts.label });
  const val = UI.el("div", { class: "kval" });
  const el = UI.el("div", { class: "knob", title: opts.doc || "" }, svg, label, val);
  const bipolar = m.min < 0 && m.max > 0 && !m.log;

  const draw = () => {
    if (value !== undefined && value !== null && m.soft) {
      if (value > m.max) m.max = value;
      if (value < m.min && !(m.log && value <= 0)) m.min = value;
    }
    const n = UI.toNorm(value || 0, m);
    const a = A0 + n * (A1 - A0);
    const a0 = bipolar ? A0 + UI.toNorm(0, m) * (A1 - A0) : A0;
    arc.setAttribute("d", Math.abs(a - a0) < 0.01 ? "" : arcPath(18, 18, 14, Math.min(a0, a), Math.max(a0, a)));
    ptr.setAttribute("x1", 18 + 4 * Math.cos(a));
    ptr.setAttribute("y1", 18 + 4 * Math.sin(a));
    ptr.setAttribute("x2", 18 + 9 * Math.cos(a));
    ptr.setAttribute("y2", 18 + 9 * Math.sin(a));
    if (!val.querySelector("input")) val.textContent = UI.fmt(value, m);
  };

  const commit = (v) => {
    if (m.int) v = Math.round(v);
    value = v;
    draw();
    opts.onchange(v);
  };

  let startY = 0, startN = 0, moved = false;
  el.addEventListener("pointerdown", (e) => {
    if (e.target.tagName === "INPUT") return;
    if (e.button !== 0) return;
    e.preventDefault();
    el.setPointerCapture(e.pointerId);
    startY = e.clientY;
    startN = UI.toNorm(value || 0, m);
    moved = false;
    el.classList.add("active");
    DAW.dragging++;
  });
  el.addEventListener("pointermove", (e) => {
    if (!el.classList.contains("active")) return;
    const dy = startY - e.clientY;
    if (Math.abs(dy) > 1) moved = true;
    if (!moved) return;
    const scale = e.shiftKey ? 1200 : 180;
    commit(UI.fromNorm(startN + dy / scale, m));
  });
  const end = () => {
    if (!el.classList.contains("active")) return;
    el.classList.remove("active");
    DAW.dragging--;
  };
  el.addEventListener("pointerup", end);
  el.addEventListener("pointercancel", end);
  el.addEventListener("dblclick", (e) => {
    if (e.target.tagName === "INPUT") return;
    if (m.def !== undefined) commit(m.def);
  });
  el.addEventListener("wheel", (e) => {
    if (Math.abs(e.deltaX) > Math.abs(e.deltaY)) return;
    e.preventDefault();
    const step = (e.shiftKey ? 0.002 : 0.02) * Math.sign(-e.deltaY);
    commit(UI.fromNorm(UI.toNorm(value || 0, m) + step, m));
  }, { passive: false });
  val.addEventListener("click", (e) => {
    if (moved) return;
    e.stopPropagation();
    const input = UI.el("input", { value: String(Math.round((value || 0) * 10000) / 10000) });
    val.textContent = "";
    val.appendChild(input);
    input.focus();
    input.select();
    const done = (apply) => {
      if (!input.parentNode) return;
      const v = parseFloat(input.value);
      val.removeChild(input);
      if (apply && !Number.isNaN(v)) commit(v); else draw();
    };
    input.addEventListener("keydown", (ev) => {
      if (ev.key === "Enter") done(true);
      if (ev.key === "Escape") done(false);
      ev.stopPropagation();
    });
    input.addEventListener("blur", () => done(true));
  });

  el.set = (v) => { if (v !== value) { value = v; draw(); } };
  draw();
  return el;
};

UI.dbToGain = (db) => db <= -60 ? 0 : Math.pow(10, db / 20);
UI.gainToDb = (g) => g <= 0.001 ? -60 : 20 * Math.log10(g);
const FADER_MAX_DB = 12;
UI.faderNorm = (g) => {
  const db = UI.gainToDb(g);
  if (db <= -60) return 0;
  const t = (db + 60) / (60 + FADER_MAX_DB);
  return Math.pow(t, 2);
};
UI.faderGain = (n) => {
  if (n <= 0.001) return 0;
  const db = Math.sqrt(n) * (60 + FADER_MAX_DB) - 60;
  return Math.min(4, UI.dbToGain(db));
};

UI.fader = (opts) => {
  let value = opts.value;
  const track = UI.el("div", { class: "track" });
  const unity = UI.el("div", { class: "unity" });
  const cap = UI.el("div", { class: "cap" });
  const el = UI.el("div", { class: "fader", title: "Volume: drag, shift for fine, double click for 0 dB" },
    track, unity, cap);
  const place = () => {
    const h = el.clientHeight - 16;
    if (h <= 0) { requestAnimationFrame(place); return; }
    cap.style.top = (8 + (1 - UI.faderNorm(value)) * h) + "px";
    unity.style.top = (8 + (1 - UI.faderNorm(1)) * h) + "px";
    if (opts.onlabel) opts.onlabel(value);
  };
  let startY = 0, startN = 0;
  el.addEventListener("pointerdown", (e) => {
    e.preventDefault();
    el.setPointerCapture(e.pointerId);
    el.classList.add("active");
    DAW.dragging++;
    startY = e.clientY;
    startN = UI.faderNorm(value);
    if (e.target !== cap) {
      const r = el.getBoundingClientRect();
      startN = 1 - (e.clientY - r.top - 8) / (r.height - 16);
      value = UI.faderGain(Math.max(0, Math.min(1, startN)));
      place();
      opts.onchange(value);
    }
  });
  el.addEventListener("pointermove", (e) => {
    if (!el.classList.contains("active")) return;
    const h = el.clientHeight - 16;
    const scale = e.shiftKey ? 6 : 1;
    const n = Math.max(0, Math.min(1, startN + (startY - e.clientY) / h / scale));
    value = UI.faderGain(n);
    place();
    opts.onchange(value);
  });
  const end = () => {
    if (!el.classList.contains("active")) return;
    el.classList.remove("active");
    DAW.dragging--;
  };
  el.addEventListener("pointerup", end);
  el.addEventListener("pointercancel", end);
  el.addEventListener("dblclick", () => { value = 1; place(); opts.onchange(1); });
  el.set = (v) => { if (v !== value) { value = v; place(); } };
  requestAnimationFrame(place);
  new ResizeObserver(() => { if (el.clientHeight > 16) place(); }).observe(el);
  return el;
};

UI.meters = new Set();

const METER_TICKS = [0, -6, -12, -24, -48];

UI.meter = (path, opts) => {
  const c = UI.el("div", { class: "meter" });
  c.path = path;
  c.opts = opts || {};
  c.peakHold = [0, 0];
  c.peakTime = [0, 0];
  c.smooth = [0, 0];
  c.last = 0;
  c.chans = [0, 1].map(() => {
    const pk = UI.el("div", { class: "mpk" });
    const rms = UI.el("div", { class: "mrms" });
    const hold = UI.el("div", { class: "mhold" });
    c.appendChild(UI.el("div", { class: "mch" }, pk, rms, hold));
    return { pk, rms, hold };
  });
  if (!c.opts.noScale) {
    for (const db of METER_TICKS) {
      c.appendChild(UI.el("div", { class: "mtick", style: { bottom: ((db + 60) / 66 * 100) + "%" } }));
    }
  }
  UI.meters.add(c);
  return c;
};

const meterDb = (v) => v <= 0.00001 ? -90 : 20 * Math.log10(v);
const meterNorm = (v) => Math.max(0, Math.min(1, (meterDb(v) + 60) / 66));
const meterClip = (v) => `inset(${((1 - v) * 100).toFixed(2)}% 0 0 0)`;

UI.meterSrc = null;

UI.drawMeters = () => {
  if (UI.meterSrc === DAW.meters) return;
  UI.meterSrc = DAW.meters;
  const now = performance.now();
  for (const c of UI.meters) {
    if (!c.isConnected) { UI.meters.delete(c); continue; }
    const dt = c.last ? Math.min(0.5, (now - c.last) / 1000) : 0;
    c.last = now;
    const m = DAW.meters[c.path];
    const peaks = m ? [m.peak_left, m.peak_right] : [0, 0];
    const rms = m ? [m.rms_left, m.rms_right] : [0, 0];
    for (let i = 0; i < 2; i++) {
      const ch = c.chans[i];
      const p = meterNorm(peaks[i]);
      c.smooth[i] = Math.max(p, c.smooth[i] - 1.5 * dt);
      if (p >= c.peakHold[i] || now - c.peakTime[i] > 1200) { c.peakHold[i] = p; c.peakTime[i] = now; }
      ch.pk.style.clipPath = meterClip(c.smooth[i]);
      ch.rms.style.clipPath = meterClip(meterNorm(rms[i]));
      ch.hold.style.bottom = (c.peakHold[i] * 100).toFixed(2) + "%";
      ch.hold.classList.toggle("hidden", c.peakHold[i] <= 0.01);
      ch.hold.classList.toggle("clip", peaks[i] >= 1 || c.peakHold[i] >= meterNorm(1));
    }
  }
};

UI.toggle = (opts) => {
  const b = UI.el("button", { class: (opts.cls || "small") + (opts.value ? " on" : ""), title: opts.doc || "" }, opts.label);
  let value = !!opts.value;
  b.addEventListener("click", (e) => {
    e.stopPropagation();
    value = !value;
    b.classList.toggle("on", value);
    opts.onchange(value);
  });
  b.set = (v) => { value = !!v; b.classList.toggle("on", value); };
  return b;
};

UI.select = (opts) => {
  const s = UI.el("select", { title: opts.doc || "" });
  for (const o of opts.options) {
    const value = typeof o === "string" ? o : o.value;
    const label = typeof o === "string" ? o : o.label;
    s.appendChild(UI.el("option", { value }, label));
  }
  if (opts.value !== undefined && !opts.options.some((o) => (typeof o === "string" ? o : o.value) === opts.value)) {
    s.appendChild(UI.el("option", { value: opts.value }, String(opts.value)));
  }
  s.value = opts.value === undefined ? "" : opts.value;
  s.addEventListener("change", () => opts.onchange(s.value));
  s.addEventListener("pointerdown", (e) => e.stopPropagation());
  s.set = (v) => {
    if (v === undefined || v === null) return;
    if (![...s.options].some((o) => o.value === String(v))) s.appendChild(UI.el("option", { value: v }, String(v)));
    s.value = v;
  };
  return s;
};

UI.enumLabel = (v) => String(v)
  .replace(/^(Audio(Channel|Eq|Mod|Emitter|Stream|Quantize|Song)?|Synth(Filter|Lfo|Source|Target|Noise|Slope)?|Instrument(Spread)?|Track(Swing)?|Saturator|FrequencyShifter|Frequency|Song|Sound|Wavetable|Curve)(?=[A-Z0-9])/, "");

UI.dragNum = (opts) => {
  let value = opts.value;
  const m = opts.meta || {};
  const el = UI.el("div", { class: "drag-num", title: opts.doc || "drag or click to type" });
  const draw = () => { if (!el.querySelector("input")) el.textContent = opts.format ? opts.format(value) : UI.fmt(value, m); };
  let startY = 0, startV = 0, moved = false;
  el.addEventListener("pointerdown", (e) => {
    if (e.target.tagName === "INPUT") return;
    el.setPointerCapture(e.pointerId);
    el.classList.add("active");
    DAW.dragging++;
    startY = e.clientY;
    startV = value || 0;
    moved = false;
  });
  el.addEventListener("pointermove", (e) => {
    if (!el.classList.contains("active")) return;
    const dy = startY - e.clientY;
    if (Math.abs(dy) > 2) moved = true;
    if (!moved) return;
    let v = startV + dy * (opts.step || 1) * (e.shiftKey ? 0.1 : 1);
    if (m.min !== undefined) v = Math.max(m.min, v);
    if (m.max !== undefined && !m.soft) v = Math.min(m.max, v);
    if (opts.int) v = Math.round(v);
    else v = Math.round(v * 1000) / 1000;
    if (v !== value) { value = v; draw(); opts.onchange(v); }
  });
  const end = () => {
    if (!el.classList.contains("active")) return;
    el.classList.remove("active");
    DAW.dragging--;
    if (!moved) {
      const input = UI.el("input", { value: String(value) });
      el.textContent = "";
      el.appendChild(input);
      input.focus();
      input.select();
      const done = (apply) => {
        if (!input.parentNode) return;
        const v = parseFloat(input.value);
        el.removeChild(input);
        if (apply && !Number.isNaN(v)) { value = opts.int ? Math.round(v) : v; opts.onchange(value); }
        draw();
      };
      input.addEventListener("keydown", (ev) => {
        ev.stopPropagation();
        if (ev.key === "Enter") done(true);
        if (ev.key === "Escape") done(false);
      });
      input.addEventListener("blur", () => done(true));
    }
  };
  el.addEventListener("pointerup", end);
  el.addEventListener("pointercancel", end);
  el.set = (v) => { if (v !== value) { value = v; draw(); } };
  draw();
  return el;
};

UI.menu = (x, y, items) => {
  UI.closeMenu();
  const m = UI.el("div", { class: "menu" });
  for (const it of items) {
    if (!it) { m.appendChild(UI.el("div", { class: "msep" })); continue; }
    m.appendChild(UI.el("div", { onclick: () => { UI.closeMenu(); it.action(); } }, it.label));
  }
  document.body.appendChild(m);
  const r = m.getBoundingClientRect();
  m.style.left = Math.min(x, window.innerWidth - r.width - 4) + "px";
  m.style.top = Math.min(y, window.innerHeight - r.height - 4) + "px";
  UI.openMenu = m;
  setTimeout(() => document.addEventListener("pointerdown", UI.menuAway, true), 0);
};

UI.menuAway = (e) => {
  if (UI.openMenu && !UI.openMenu.contains(e.target)) UI.closeMenu();
};

UI.closeMenu = () => {
  if (UI.openMenu) UI.openMenu.remove();
  UI.openMenu = null;
  document.removeEventListener("pointerdown", UI.menuAway, true);
};

UI.entityOptions = (member, struct) => {
  let recs;
  const n = member.toLowerCase();
  if (["output", "bus", "key", "sidechain"].includes(n)) recs = DAW.buses();
  else if (n === "sound") recs = DAW.sounds();
  else if (n === "instrument") recs = DAW.list("Instrument");
  else if (n === "clip") recs = DAW.clips();
  else if (n === "target") recs = Object.values(DAW.ents).filter((r) => DAW.has(r, "Bus") || DAW.isEffect(r));
  else if (n === "table") recs = DAW.list("Wavetable");
  else if (n === "component") return null;
  else recs = Object.values(DAW.ents);
  const opts = [{ value: "#0", label: n === "output" ? "master" : "(none)" }];
  for (const r of recs) opts.push({ value: r.path, label: r.path });
  return opts;
};

UI.member = (ctx) => {
  const { path, comp, keys, type, struct, name, value, onchange } = ctx;
  const kind = Array.isArray(type) ? type[0] : "struct";
  const doc = UI.doc(struct, name);
  const label = name.replace(/_/g, " ");
  if (kind === "float" || kind === "double") {
    const k = UI.knob({ label, meta: UI.meta(struct, name), value, doc, onchange });
    UI.bind(path, comp, keys, (v) => k.set(v), k);
    return k;
  }
  if (/^u?int\d*$/.test(kind) || kind === "i32" || kind === "u32") {
    const m = UI.meta(struct, name);
    const range = window.BOX_AUDIO_PARAMS[struct] && window.BOX_AUDIO_PARAMS[struct][name];
    const k = UI.knob({
      label, value, doc, onchange,
      meta: Object.assign({}, m, { int: true, soft: true, max: range && range.max !== undefined ? m.max : Math.max(16, value * 2) })
    });
    UI.bind(path, comp, keys, (v) => k.set(v), k);
    return k;
  }
  if (kind === "bool") {
    const t = UI.toggle({ label, value, doc, onchange });
    UI.bind(path, comp, keys, (v) => t.set(v), t);
    return UI.el("div", { class: "field toggle" }, t);
  }
  if (kind === "enum" || kind === "bitmask") {
    const s = UI.select({
      options: type.slice(1).map((o) => ({ value: o, label: UI.enumLabel(o) })), value, doc, onchange
    });
    UI.bind(path, comp, keys, (v) => s.set(v), s);
    return UI.el("div", { class: "field", title: doc }, label, s);
  }
  if (kind === "entity") {
    const opts = UI.entityOptions(name, struct);
    if (opts) {
      const s = UI.select({ options: opts, value: DAW.isNone(value) ? "#0" : value, doc, onchange });
      UI.bind(path, comp, keys, (v) => s.set(DAW.isNone(v) ? "#0" : v), s);
      return UI.el("div", { class: "field", title: doc }, label, s);
    }
  }
  if (kind === "text" || kind === "string" || kind === "entity") {
    const input = UI.el("input", { value: value === null || value === undefined ? "" : value, title: doc });
    input.addEventListener("keydown", (e) => {
      e.stopPropagation();
      if (e.key === "Enter") { input.blur(); }
    });
    input.addEventListener("change", () => onchange(input.value));
    UI.bind(path, comp, keys, (v) => { if (document.activeElement !== input) input.value = v === null ? "" : v; }, input);
    return UI.el("div", { class: "field", title: doc }, label, input);
  }
  if (kind === "array" && Array.isArray(type[1]) && (type[1][0] === "float") && type[2] >= 8) {
    return UI.steps({ path, comp, keys, value, label, struct, name, onchange, doc });
  }
  if (kind === "array" || kind === "vector") {
    const sub = type[1];
    const nested = NESTED[struct + "." + name] || struct;
    const list = value || [];
    const head = UI.el("div", { class: "glabel", title: doc }, label);
    const box = UI.el("div", { class: "group-box" }, head);
    const edit = (fn) => {
      const v = UI.currentValue(path, comp, keys) || DAW.clone(list);
      fn(v);
      onchange(v);
    };
    if (kind === "vector") {
      head.appendChild(UI.el("button", {
        class: "small", text: "+", title: "Add an element",
        onclick: () => edit((v) => v.push(UI.defaultOf(sub, nested)))
      }));
    }
    list.forEach((item, i) => {
      const childChange = (nv) => edit((v) => { v[i] = nv; });
      if (Array.isArray(sub)) {
        box.appendChild(UI.member({
          path, comp, keys: keys.concat([i]), type: sub, struct: nested,
          name: name + " " + (i + 1), value: item, onchange: childChange
        }));
        return;
      }
      const gl = UI.el("div", { class: "glabel" }, `${label} ${i + 1}`);
      if (kind === "vector") {
        gl.appendChild(UI.el("button", {
          class: "small", text: "x", title: "Remove",
          onclick: () => edit((v) => v.splice(i, 1))
        }));
      }
      const wrap = UI.el("div", { class: "group-box" }, gl);
      for (const mname in sub) {
        wrap.appendChild(UI.member({
          path, comp, keys: keys.concat([i, mname]), type: sub[mname], struct: nested, name: mname,
          value: item ? item[mname] : undefined,
          onchange: (nv) => edit((v) => { v[i][mname] = nv; })
        }));
      }
      box.appendChild(wrap);
    });
    return box;
  }
  if (kind === "struct" && type && typeof type === "object") {
    if (type.kind && type.keys) {
      const n = value && value.keys ? value.keys.length : 0;
      return UI.el("div", { class: "field", title: doc }, label, UI.el("span", { style: { color: "var(--fg2)" } }, `${UI.enumLabel(value ? value.kind : "")} curve, ${n} keys`));
    }
    const nested = NESTED[struct + "." + name] || name.replace(/(^|_)(\w)/g, (a, b, c) => c.toUpperCase());
    const box = UI.el("div", { class: "group-box" }, UI.el("div", { class: "glabel", title: doc }, label));
    for (const mname in type) {
      box.appendChild(UI.member({
        path, comp, keys: keys.concat([mname]), type: type[mname], struct: window.BOX_AUDIO_PARAMS[nested] ? nested : struct,
        name: mname, value: value ? value[mname] : undefined,
        onchange: (nv) => {
          const v = DAW.clone(UI.currentValue(path, comp, keys) || value || {});
          v[mname] = nv;
          onchange(v);
        }
      }));
    }
    return box;
  }
  return UI.el("div", { class: "field" }, label, UI.el("span", { style: { color: "var(--fg2)" } }, JSON.stringify(value)));
};

UI.currentValue = (path, comp, keys) => {
  const s = DAW.short(comp);
  const base = path === A + s && DAW.single[s] ? DAW.single[s] : (DAW.ents[path] ? DAW.ents[path].comps[comp] : undefined);
  return DAW.clone(UI.dig(base, keys));
};

UI.defaultOf = (type, struct) => {
  if (!Array.isArray(type)) {
    const o = {};
    for (const k in type) {
      const t = type[k];
      if (Array.isArray(t) && (t[0] === "float" || t[0] === "double")) {
        const m = UI.meta(struct, k);
        o[k] = m.def !== undefined ? m.def : 0;
      } else if (Array.isArray(t) && t[0] === "entity") o[k] = "#0";
      else if (Array.isArray(t) && t[0] === "enum") o[k] = t[1];
      else if (Array.isArray(t) && t[0] === "bool") o[k] = false;
      else if (Array.isArray(t)) o[k] = 0;
      else o[k] = UI.defaultOf(t, struct);
    }
    return o;
  }
  if (type[0] === "entity") return "#0";
  if (type[0] === "bool") return false;
  return 0;
};

UI.steps = (ctx) => {
  const { path, comp, keys, label, struct, name, onchange, doc } = ctx;
  let value = (ctx.value || []).slice();
  const m = UI.meta(struct, name);
  const bipolar = m.min < 0;
  const bars = UI.el("div", { class: "steps" + (bipolar ? " bipolar" : "") });
  const cells = value.map(() => UI.el("div"));
  cells.forEach((c) => bars.appendChild(c));
  let count = value.length;
  const draw = () => {
    cells.forEach((c, i) => {
      const v = value[i] || 0;
      const n = (v - m.min) / (m.max - m.min);
      if (bipolar) {
        const z = (0 - m.min) / (m.max - m.min);
        c.style.height = Math.max(1, Math.abs(n - z) * 46) + "px";
        c.style.marginBottom = (Math.min(n, z) * 46) + "px";
      } else {
        c.style.height = Math.max(1, n * 46) + "px";
        c.style.marginBottom = "0";
      }
      c.classList.toggle("dim", i >= count);
    });
  };
  const setAt = (e) => {
    const r = bars.getBoundingClientRect();
    const i = Math.floor((e.clientX - r.left - 2) / 14);
    if (i < 0 || i >= value.length) return;
    const n = 1 - (e.clientY - r.top - 2) / 46;
    let v = m.min + Math.max(0, Math.min(1, n)) * (m.max - m.min);
    v = Math.round(v * 100) / 100;
    if (value[i] === v) return;
    value[i] = v;
    draw();
    onchange(value.slice());
  };
  bars.addEventListener("pointerdown", (e) => {
    e.preventDefault();
    bars.setPointerCapture(e.pointerId);
    bars.classList.add("active");
    DAW.dragging++;
    setAt(e);
  });
  bars.addEventListener("pointermove", (e) => { if (bars.classList.contains("active")) setAt(e); });
  const end = () => { if (bars.classList.contains("active")) { bars.classList.remove("active"); DAW.dragging--; } };
  bars.addEventListener("pointerup", end);
  bars.addEventListener("pointercancel", end);
  UI.bind(path, comp, keys, (v) => { if (v) { value = v.slice(); draw(); } }, bars);
  const rec = DAW.ents[path];
  const countComp = rec && rec.comps[comp];
  if (countComp && countComp.step_count !== undefined) {
    count = countComp.step_count;
    UI.bind(path, comp, ["step_count"], (v) => { count = v; draw(); }, bars);
  }
  draw();
  return UI.el("div", { class: "field", title: doc }, label, bars);
};

UI.componentBody = (path, comp, opts) => {
  opts = opts || {};
  const s = DAW.short(comp);
  const single = path === A + s;
  const value = single ? DAW.single[s] : (DAW.ents[path] ? DAW.ents[path].comps[comp] : undefined);
  const type = DAW.types[comp];
  const body = UI.el("div", { class: "card-body" + (opts.wide ? " wide" : "") });
  if (!type || typeof type !== "object" || value === null || value === undefined) {
    body.appendChild(UI.el("span", { style: { color: "var(--fg2)" } }, value === null ? "(no value)" : "(loading type)"));
    return body;
  }
  for (const name in type) {
    if (opts.skip && opts.skip.includes(name)) continue;
    body.appendChild(UI.member({
      path, comp, keys: [name], type: type[name], struct: s, name,
      value: value[name],
      onchange: (v) => DAW.write(path, comp, { [name]: v })
    }));
  }
  return body;
};

UI.card = (path, comp, opts) => {
  opts = opts || {};
  const rec = DAW.ents[path];
  const s = DAW.short(comp);
  const head = UI.el("div", { class: "card-head", title: (window.BOX_AUDIO_DOCS || {})[s] || "" },
    UI.el("span", { class: "name" }, opts.title || s),
    opts.title ? UI.el("span", { class: "comp" }, s) : null,
    UI.el("span", { class: "grow" }), ...(opts.buttons || []));
  const card = UI.el("div", { class: "card" + (rec && rec.disabled ? " off" : "") + (opts.sel ? " sel" : "") },
    head, opts.graph || null, UI.componentBody(path, comp, opts));
  return card;
};
