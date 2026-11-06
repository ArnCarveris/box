"use strict";

const A = "box.audio.";

const EFFECTS = [
  "Reverb", "Equalizer", "AudioFilter", "Compressor", "Gate", "Delay", "Phaser", "Chorus",
  "Saturator", "TransientShaper", "Vocoder", "FrequencyShifter", "PitchShifter", "PatternGate",
  "Rotary", "BeatRepeat", "TapeStop"
];

const NOT_EFFECTS = new Set([
  "AudioBus", "AudioMeter", "AudioOutput", "Track", "AudioEffectState", "AudioBusState",
  "AudioTrackState", "Instrument", "AudioSource", "AudioStream", "AudioSpatial", "AudioModulator",
  "AudioMacro", "AudioMacroMap", "ClipPlacement", "NoteClip", "AudioClip", "AutomationClip"
]);

const STRUCTURE_QUERIES = [
  { expr: A + "AudioBus" },
  { expr: A + "AudioEffectState" },
  { expr: A + "ClipPlacement" },
  { expr: A + "NoteClip || " + A + "AudioClip || " + A + "AutomationClip" },
  { expr: A + "Instrument" },
  { expr: A + "SynthSound || " + A + "SoundFile" },
  { expr: A + "SynthOscillator || " + A + "SynthModRoute || " + A + "SynthLfo" },
  { expr: A + "AudioModulator || " + A + "AudioMacro || " + A + "AudioMacroMap" },
  { expr: A + "AudioSource || " + A + "AudioStream" },
  { expr: A + "AudioPcm", values: false },
  { expr: A + "Wavetable", values: false }
];

const SINGLETONS = ["Transport", "Song", "AudioSong", "AudioSongStatus", "AudioSettings", "AudioStatus"];

const DAW = window.DAW = {
  conn: null,
  host: null,
  connected: false,
  hasAudio: false,
  ents: {},
  types: {},
  single: {},
  meters: {},
  sel: null,
  selKind: null,
  view: "graph",
  detailPath: new URLSearchParams(location.search).get("entity"),
  sig: "",
  bindings: [],
  dragging: 0,
  pending: {},
  epoch: 0,
  requestedMeters: new Set(),
  auditionPath: "daw_audition",
  listeners: [],
  lastPos: { pos: 0, time: 0, playing: false }
};

DAW.short = (comp) => comp && comp.startsWith(A) ? comp.slice(A.length) : comp;
DAW.full = (short) => short.includes(".") ? short : A + short;
DAW.leaf = (path) => {
  if (!path) return "";
  const i = path.lastIndexOf(".");
  return i < 0 ? path : path.slice(i + 1);
};
DAW.isNone = (ref) => !ref || ref === "#0" || ref === 0;

DAW.hidden = (comp) => /State$/.test(comp) || /^flecs\./.test(comp) ||
  ["box.audio.AudioVoice", "box.audio.AudioSoundId", "box.audio.AudioSynth"].includes(comp);

DAW.on = (fn) => DAW.listeners.push(fn);
DAW.emit = (what) => { for (const fn of DAW.listeners) fn(what); };

DAW.toast = (msg, err) => {
  const el = document.getElementById("toast");
  el.textContent = msg;
  el.className = "show" + (err ? " err" : "");
  clearTimeout(DAW.toastTimer);
  DAW.toastTimer = setTimeout(() => { el.className = ""; }, err ? 5000 : 2200);
};

DAW.clone = (v) => v === undefined ? v : JSON.parse(JSON.stringify(v));

DAW.isEffect = (rec) => {
  if (!rec) return false;
  return Object.keys(rec.comps).some((c) => DAW.effectComp(c));
};

DAW.effectComp = (comp) => {
  const s = DAW.short(comp);
  if (EFFECTS.includes(s)) return true;
  return false;
};

DAW.effectComps = (rec) => rec ? Object.keys(rec.comps).filter((c) => DAW.effectComp(c)) : [];

DAW.has = (rec, short) => !!rec && rec.comps[A + short] !== undefined;
DAW.get = (path) => DAW.ents[path];
DAW.val = (path, short) => {
  const rec = DAW.ents[path];
  return rec ? rec.comps[A + short] : undefined;
};

DAW.list = (short, filter) => Object.values(DAW.ents)
  .filter((r) => r.comps[A + short] !== undefined && (!filter || filter(r)))
  .sort((a, b) => a.order - b.order);

DAW.buses = () => DAW.list("AudioBus");
DAW.tracks = () => DAW.list("Track");
DAW.effects = () => Object.values(DAW.ents).filter((r) => DAW.isEffect(r));
DAW.clips = () => Object.values(DAW.ents).filter((r) =>
  DAW.has(r, "NoteClip") || DAW.has(r, "AudioClip") || DAW.has(r, "AutomationClip"));
DAW.sounds = () => Object.values(DAW.ents).filter((r) =>
  (DAW.has(r, "SynthSound") || DAW.has(r, "SoundFile") || DAW.has(r, "AudioPcm")));
DAW.placementsOf = (track) => Object.values(DAW.ents)
  .filter((r) => r.parent === track && DAW.has(r, "ClipPlacement"));
DAW.childrenOf = (path) => Object.values(DAW.ents).filter((r) => r.parent === path)
  .sort((a, b) => a.order - b.order);
DAW.isAudition = (path) => /^daw_audition(_\w+)?(\.|$)/.test(path || "");

DAW.busOfEffect = (path) => DAW.buses().find((b) => {
  const fx = b.comps[A + "AudioBus"].effects || [];
  return fx.includes(path);
});

DAW.kindOf = (rec) => {
  if (!rec) return null;
  if (DAW.has(rec, "Track")) return "track";
  if (DAW.has(rec, "AudioBus")) return "bus";
  if (DAW.has(rec, "ClipPlacement")) return "placement";
  if (DAW.has(rec, "NoteClip") || DAW.has(rec, "AutomationClip") || DAW.has(rec, "AudioClip")) return "clip";
  if (DAW.isEffect(rec)) return "effect";
  if (DAW.has(rec, "Instrument")) return "instrument";
  if (DAW.has(rec, "SynthSound") || DAW.has(rec, "SoundFile") || DAW.has(rec, "AudioPcm")) return "sound";
  if (DAW.has(rec, "AudioMacro")) return "macro";
  if (DAW.has(rec, "AudioModulator")) return "modulator";
  if (DAW.has(rec, "AudioSource") || DAW.has(rec, "AudioStream")) return "source";
  if (DAW.has(rec, "SynthOscillator")) return "layer";
  return "entity";
};

DAW.color = (name) => {
  let h = 0;
  for (const c of name || "") h = (h * 31 + c.charCodeAt(0)) >>> 0;
  return `hsl(${h % 360}, 62%, 58%)`;
};

function query(expr, params) {
  return new Promise((resolve, reject) => {
    const conn = DAW.conn;
    if (!conn) { reject("no connection"); return; }
    const p = Object.assign({ values: true, table: true }, params || {});
    const full = expr + ", ?flecs.core.Prefab, ?flecs.core.Disabled";
    conn.query(full, p, (r) => resolve(r), (e) => reject(e));
  });
}

function getComponent(short) {
  return new Promise((resolve) => {
    const conn = DAW.conn;
    if (!conn) { resolve(undefined); return; }
    conn._.request(conn, "GET", "component/" + conn._.escapePath(A + short),
      { component: A + short },
      (msg) => { try { resolve(JSON.parse(msg)); } catch (e) { resolve(undefined); } },
      () => resolve(undefined));
  });
}

DAW.request = (method, path, params) => new Promise((resolve, reject) => {
  const conn = DAW.conn;
  if (!conn) { reject("no connection"); return; }
  conn._.request(conn, method, path, params || {},
    (msg) => resolve(msg),
    (msg) => {
      let text = msg;
      try { text = JSON.parse(msg).error || msg; } catch (e) { }
      reject(text);
    });
});

DAW.entityPath = (path) => DAW.conn._.escapePath(path);

DAW.write = (path, comp, partial) => {
  if (!DAW.conn || !path) return;
  const key = path + "|" + comp;
  let w = DAW.pending[key];
  if (!w) w = DAW.pending[key] = { path, comp, value: {}, recent: {}, inflight: false, t: 0 };
  Object.assign(w.value, DAW.clone(partial));
  Object.assign(w.recent, DAW.clone(partial));
  w.t = performance.now();
  const rec = DAW.ents[path];
  if (rec && rec.comps[comp]) Object.assign(rec.comps[comp], DAW.clone(partial));
  const single = DAW.short(comp);
  if (path === A + single && DAW.single[single]) Object.assign(DAW.single[single], DAW.clone(partial));
  flush(w);
};

function flush(w) {
  if (w.inflight || !Object.keys(w.value).length) return;
  const v = w.value;
  w.value = {};
  w.inflight = true;
  const epoch = DAW.epoch;
  DAW.request("PUT", "component/" + DAW.entityPath(w.path), {
    component: w.comp,
    value: encodeURIComponent(JSON.stringify(v))
  }).then(() => {
    w.inflight = false;
    w.t = performance.now();
    if (epoch === DAW.epoch) flush(w);
  }, (err) => {
    w.inflight = false;
    DAW.toast(`set ${DAW.short(w.comp)} on ${w.path} failed:\n${err}`, true);
    if (epoch === DAW.epoch) flush(w);
  });
}

function overlayPending() {
  const now = performance.now();
  for (const key in DAW.pending) {
    const w = DAW.pending[key];
    if (!w.inflight && now - w.t > 700 && !Object.keys(w.value).length) {
      delete DAW.pending[key];
      continue;
    }
    const rec = DAW.ents[w.path];
    if (rec && rec.comps[w.comp]) Object.assign(rec.comps[w.comp], DAW.clone(w.recent));
    const single = DAW.short(w.comp);
    if (w.path === A + single && DAW.single[single]) {
      Object.assign(DAW.single[single], DAW.clone(w.recent));
    }
  }
}

DAW.isPending = (path, comp) => {
  const w = DAW.pending[path + "|" + comp];
  return !!w && (w.inflight || performance.now() - w.t < 700);
};

DAW.add = (path, comp) => DAW.request("PUT", "component/" + DAW.entityPath(path), { component: comp });
DAW.removeComp = (path, comp) => DAW.request("DELETE", "component/" + DAW.entityPath(path), { component: comp });
DAW.create = (path) => DAW.request("PUT", "entity/" + DAW.entityPath(path), {});
DAW.del = (path) => DAW.request("DELETE", "entity/" + DAW.entityPath(path), {});
DAW.enable = (path, on) => DAW.request("PUT", "toggle/" + DAW.entityPath(path), { enable: !!on });

DAW.uniqueName = (parent, base) => {
  let i = 1;
  const prefix = parent ? parent + "." : "";
  while (DAW.ents[prefix + base + "_" + i]) i++;
  return prefix + base + "_" + i;
};

function parse(results, into, order) {
  for (const r of results || []) {
    const path = r.parent ? r.parent + "." + r.name : r.name;
    if (!path) continue;
    let rec = into[path];
    if (!rec) {
      rec = into[path] = {
        path, name: r.name, parent: r.parent, comps: {}, tags: r.tags || [], order: order.n++
      };
    }
    rec.prefab = (r.tags || []).includes("flecs.core.Prefab");
    rec.disabled = (r.tags || []).includes("flecs.core.Disabled");
    if (r.pairs && r.pairs["flecs.script.Script"]) rec.script = r.pairs["flecs.script.Script"];
    if (r.components) {
      for (const c in r.components) {
        if (c === "flecs.core.Parent" || c === "flecs.core.Identifier") continue;
        rec.comps[c] = r.components[c];
      }
    }
  }
}

function signature() {
  const parts = [];
  const walk = (v) => {
    if (Array.isArray(v)) { parts.push("[" + v.length); v.forEach(walk); parts.push("]"); }
    else if (v && typeof v === "object") { for (const k in v) { parts.push(k); walk(v[k]); } }
    else if (typeof v === "string") parts.push(v);
  };
  for (const path of Object.keys(DAW.ents).sort()) {
    const rec = DAW.ents[path];
    parts.push("|" + path + (rec.disabled ? "!" : ""));
    for (const c of Object.keys(rec.comps).sort()) {
      if (c === A + "AudioMeter") continue;
      parts.push(c);
      if (c === A + "NoteClip" || c === A + "AutomationClip") continue;
      walk(rec.comps[c]);
    }
  }
  const song = DAW.single.Song;
  if (song) walk(song.lanes);
  parts.push(DAW.single.AudioSongStatus ? DAW.single.AudioSongStatus.state : "");
  return parts.join(",");
}

async function pollStructure(epoch) {
  if (epoch !== DAW.epoch) return;
  if (DAW.connected) {
    try {
      const ents = {};
      const order = { n: 0 };
      const needTypes = DAW.typesDirty;
      const replies = await Promise.all(STRUCTURE_QUERIES.map((q) => query(q.expr, {
        values: q.values !== false,
        table: q.values !== false,
        type_info: needTypes && q.values !== false
      }).catch((e) => ({ error: e }))));
      if (epoch !== DAW.epoch) return;
      const singles = await Promise.all(SINGLETONS.map((s) => getComponent(s)));
      if (epoch !== DAW.epoch) return;
      replies.forEach((r) => {
        if (r.error) return;
        if (r.type_info) {
          for (const c in r.type_info) if (r.type_info[c]) DAW.types[c] = r.type_info[c];
        }
        parse(r.results, ents, order);
      });
      SINGLETONS.forEach((s, i) => { DAW.single[s] = singles[i]; });
      DAW.hasAudio = !replies[0].error && !!singles[SINGLETONS.indexOf("AudioStatus")];
      for (const p of Object.keys(ents)) if (DAW.isAudition(p)) delete ents[p];
      DAW.ents = ents;
      DAW.typesDirty = Object.values(ents).some((r) =>
        Object.keys(r.comps).some((c) => r.comps[c] !== null && !DAW.types[c] && !DAW.hidden(c)));
      overlayPending();
      ensureMeters();
      const sig = signature();
      const changed = sig !== DAW.sig;
      DAW.sig = sig;
      if (DAW.sel && !DAW.ents[DAW.sel] && !DAW.detailPath) DAW.select(null);
      DAW.emit(changed ? "structure" : "values");
    } catch (e) {
      console.error(e);
    }
  }
  setTimeout(() => pollStructure(epoch), 900);
}

function ensureMeters() {
  if (!DAW.autoMeter) return;
  for (const rec of Object.values(DAW.ents)) {
    const want = DAW.has(rec, "AudioBus") || DAW.isEffect(rec);
    if (!want || DAW.has(rec, "AudioMeter") || rec.prefab) continue;
    if (DAW.requestedMeters.has(rec.path)) continue;
    DAW.requestedMeters.add(rec.path);
    DAW.add(rec.path, A + "AudioMeter").catch(() => { });
  }
}

async function pollFast(epoch) {
  if (epoch !== DAW.epoch) return;
  if (DAW.connected && DAW.hasAudio) {
    try {
      const [meters, transport] = await Promise.all([
        new Promise((resolve) => DAW.conn.query(A + "AudioMeter", { values: true },
          (r) => resolve(r), () => resolve(null))),
        getComponent("Transport")
      ]);
      if (epoch !== DAW.epoch) return;
      if (meters && meters.results) {
        const m = {};
        for (const r of meters.results) {
          const path = r.parent ? r.parent + "." + r.name : r.name;
          m[path] = r.fields && r.fields.values ? r.fields.values[0] : null;
        }
        DAW.meters = m;
      }
      if (transport) {
        if (DAW.isPending(A + "Transport", A + "Transport")) {
          const w = DAW.pending[A + "Transport|" + A + "Transport"];
          Object.assign(transport, w.recent);
        }
        const prev = DAW.single.Transport;
        DAW.single.Transport = transport;
        if (!prev || transport.position !== DAW.lastPos.pos || transport.playing !== DAW.lastPos.playing) {
          DAW.lastPos = { pos: transport.position, time: performance.now(), playing: transport.playing };
        }
      }
      DAW.emit("fast");
    } catch (e) {
      console.error(e);
    }
  }
  setTimeout(() => pollFast(epoch), 60);
}

async function pollStatus(epoch) {
  if (epoch !== DAW.epoch) return;
  if (DAW.connected) {
    const status = await getComponent("AudioStatus");
    if (epoch !== DAW.epoch) return;
    DAW.single.AudioStatus = status;
    DAW.emit("status");
  }
  setTimeout(() => pollStatus(epoch), 500);
}

DAW.position = () => {
  const t = DAW.single.Transport;
  if (!t) return 0;
  let pos = DAW.lastPos.pos;
  if (t.playing) {
    const dt = (performance.now() - DAW.lastPos.time) / 1000;
    pos += Math.min(dt, 0.25) * t.tempo / 60;
    if (t.loop && t.loop_end > t.loop_start && pos >= t.loop_end) {
      pos = t.loop_start + ((pos - t.loop_start) % (t.loop_end - t.loop_start));
    }
  }
  return pos;
};

DAW.transport = (partial) => DAW.write(A + "Transport", A + "Transport", partial);

DAW.seek = (beats) => {
  DAW.transport({ position: Math.max(0, beats) });
  DAW.lastPos = { pos: Math.max(0, beats), time: performance.now(), playing: DAW.lastPos.playing };
};

DAW.select = (path, insert) => {
  if (DAW.detailPath && path && path !== DAW.sel) {
    DAW.detailPath = path;
    const url = new URL(location.href);
    url.searchParams.set("entity", path);
    history.replaceState(null, "", url);
    document.title = `${DAW.leaf(path)} - box daw`;
  }
  DAW.sel = path;
  DAW.selInsert = !!(path && insert);
  DAW.emit("select");
};

DAW.openWindow = (path) => {
  if (!path || !DAW.host) return;
  const url = new URL(location.href);
  url.search = "";
  url.searchParams.set("host", DAW.host);
  url.searchParams.set("entity", path);
  const w = window.open(url.toString(), "daw_" + path.replace(/\W/g, "_"), "width=1100,height=760");
  if (w) w.focus();
  else DAW.toast("the browser blocked the window, allow popups for this page", true);
};

DAW.connect = (host) => {
  DAW.disconnect();
  DAW.host = host;
  DAW.epoch++;
  const epoch = DAW.epoch;
  DAW.typesDirty = true;
  localStorage.setItem("daw.host", host);
  DAW.conn = flecs.connect({
    host,
    poll_interval_ms: 1000,
    on_status: (status) => {
      if (epoch !== DAW.epoch) return;
      DAW.connected = status === flecs.ConnectionStatus.Connected;
      DAW.emit("connection");
    }
  });
  pollStructure(epoch);
  pollFast(epoch);
  pollStatus(epoch);
  DAW.emit("connection");
};

DAW.disconnect = () => {
  if (DAW.conn) {
    DAW.releaseAudition(true);
    DAW.conn.disconnect();
  }
  DAW.epoch++;
  DAW.conn = null;
  DAW.connected = false;
  DAW.hasAudio = false;
  DAW.ents = {};
  DAW.single = {};
  DAW.meters = {};
  DAW.pending = {};
  DAW.sig = "";
  DAW.sel = DAW.detailPath || null;
  DAW.requestedMeters = new Set();
  DAW.auditionReady = null;
  DAW.emit("structure");
};

DAW.ensureAudition = () => {
  if (!DAW.auditionReady) {
    DAW.auditionReady = DAW.create(DAW.auditionPath).catch(() => { });
  }
  return DAW.auditionReady;
};

DAW.voices = {};

DAW.noteOn = async (note, target, velocity) => {
  if (!target || !target.sound || !DAW.conn) return;
  await DAW.ensureAudition();
  const path = DAW.auditionPath + ".k" + Math.round(note * 100);
  const root = target.root === undefined ? 69 : target.root;
  const curve = target.velocity_curve === undefined ? 2 : target.velocity_curve;
  const vel = velocity === undefined ? 0.8 : velocity;
  if (!DAW.voices[path]) {
    DAW.voices[path] = DAW.create(path).catch(() => { });
  }
  await DAW.voices[path];
  DAW.write(path, A + "AudioOutput", { bus: target.bus || "#0" });
  DAW.write(path, A + "AudioSource", {
    sound: target.sound,
    pitch: Math.pow(2, (note - root) / 12),
    volume: Math.pow(vel, curve),
    loop: false,
    playing: true
  });
};

DAW.noteOff = (note) => {
  const path = DAW.auditionPath + ".k" + Math.round(note * 100);
  if (DAW.voices[path]) DAW.write(path, A + "AudioSource", { playing: false });
};

DAW.releaseAudition = (sync) => {
  if (!DAW.conn || !DAW.auditionReady) return;
  const url = DAW.conn.url + "/entity/" + DAW.entityPath(DAW.auditionPath);
  if (sync && typeof fetch === "function") {
    fetch(url, { method: "DELETE", keepalive: true }).catch(() => { });
  } else {
    DAW.del(DAW.auditionPath).catch(() => { });
  }
  DAW.voices = {};
  DAW.auditionReady = null;
};

DAW.capture = async (seconds) => {
  const conn = DAW.conn;
  const session = await new Promise((resolve, reject) =>
    conn.commandSessionCreate((r) => resolve(r), (e) => reject(e)));
  const id = session.session;
  const reply = await new Promise((resolve, reject) =>
    conn.commandRun(id, "audio.capture " + seconds, {}, (r) => resolve(r), (e) => reject(e)));
  if (reply.errors && reply.errors.length) throw new Error(reply.errors.join("\n"));
  const result = reply.results && reply.results[0] && reply.results[0].result;
  const name = result && result.name;
  if (!name) throw new Error("no resource in reply: " + JSON.stringify(reply));
  const deadline = performance.now() + seconds * 1000 + 15000;
  while (performance.now() < deadline) {
    await new Promise((r) => setTimeout(r, 500));
    const blob = await new Promise((resolve) =>
      conn.commandResource(id, name, (b) => resolve(b), () => resolve(null)));
    if (blob && blob.size > 44) {
      conn.commandSessionDelete(id);
      return { blob, name };
    }
  }
  conn.commandSessionDelete(id);
  throw new Error("capture timed out");
};

window.addEventListener("beforeunload", () => DAW.releaseAudition(true));
