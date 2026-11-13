"use strict";

const T = {};

function buildTopbar() {
  const tp = document.getElementById("transport");
  const t = () => DAW.single.Transport || {};
  T.play = UI.el("button", { class: "icon", title: "Play / pause (space)", text: "▶", onclick: () => DAW.transport({ playing: !t().playing }) });
  T.stop = UI.el("button", {
    class: "icon", title: "Stop and return to the start", text: "■",
    onclick: () => { DAW.transport({ playing: false }); DAW.seek(t().loop ? t().loop_start : 0); }
  });
  T.home = UI.el("button", { class: "icon", title: "Return to beat 0 (home)", text: "⏮", onclick: () => DAW.seek(0) });
  T.loop = UI.toggle({ label: "loop", value: false, onchange: (v) => DAW.transport({ loop: v }) });
  T.metro = UI.toggle({ label: "click", value: false, doc: "Metronome", onchange: (v) => DAW.transport({ metronome: v }) });
  T.pos = UI.el("div", { class: "lcd", title: "bar.beat.sixteenth" }, "1.1.1");
  T.tempo = UI.dragNum({
    value: 120, meta: { min: 20, max: 999 }, step: 0.5, format: (v) => `${(+v).toFixed(1)} bpm`,
    onchange: (v) => DAW.transport({ tempo: v })
  });
  T.bpb = UI.dragNum({ value: 4, meta: { min: 1, max: 64 }, int: true, step: 0.1, format: (v) => String(v), onchange: (v) => DAW.transport({ beats_per_bar: v }) });
  T.unit = UI.dragNum({ value: 4, meta: { min: 1, max: 64 }, int: true, step: 0.1, format: (v) => String(v), onchange: (v) => DAW.transport({ beat_unit: v }) });
  tp.append(T.home, T.play, T.stop, T.loop, T.metro, T.pos, T.tempo, T.bpb, UI.el("span", {}, "/"), T.unit);

  const sg = document.getElementById("song-group");
  T.songTitle = UI.el("span", { class: "status" });
  T.songFile = UI.el("input", { placeholder: "song .flecs file", style: { width: "220px" }, title: "SongFile.file: Enter loads, empty unloads" });
  T.songFile.addEventListener("keydown", (e) => {
    e.stopPropagation();
    if (e.key === "Enter") {
      DAW.write(A + "SongFile", A + "SongFile", { file: T.songFile.value.trim() });
      T.songFile.blur();
    }
  });
  T.songList = UI.el("datalist", { id: "song-files" });
  T.songFile.setAttribute("list", "song-files");
  sg.append(T.songTitle, T.songFile, T.songList);

  const mg = document.getElementById("master-group");
  T.master = UI.knob({
    label: "master", meta: UI.meta("Settings", "master_volume"), value: 1,
    onchange: (v) => DAW.write(A + "Settings", A + "Settings", { master_volume: v })
  });
  T.master.style.width = "50px";
  T.master.querySelector("svg").style.width = "24px";
  T.master.querySelector("svg").style.height = "24px";
  T.master.style.flexDirection = "row";
  T.master.style.gap = "4px";
  T.master.querySelector(".klabel").style.display = "none";
  T.mute = UI.toggle({ label: "mute", cls: "small mute", value: false, onchange: (v) => DAW.write(A + "Settings", A + "Settings", { mute: v }) });
  T.meter = UI.meter(A + "master", { noScale: true });
  T.meter.style.height = "26px";
  T.meter.style.width = "8px";
  T.rec = UI.el("button", { class: "small", text: "● rec 10s", title: "Capture 10 seconds of the master output to a WAV file", onclick: () => capture(10) });
  mg.append(T.master, T.mute, T.meter, T.rec);

  T.status = UI.el("span", { class: "status" });
  document.getElementById("status-group").append(T.status);
}

async function capture(seconds) {
  if (!DAW.conn) return;
  T.rec.disabled = true;
  T.rec.textContent = "recording...";
  try {
    const { blob, name } = await DAW.capture(seconds);
    const a = UI.el("a", { href: URL.createObjectURL(blob), download: `box_${name}.wav` });
    document.body.appendChild(a);
    a.click();
    a.remove();
    DAW.toast(`captured ${seconds}s to box_${name}.wav`);
  } catch (e) {
    DAW.toast("capture failed: " + (e.message || e), true);
  }
  T.rec.disabled = false;
  T.rec.textContent = "● rec 10s";
}

window.addEventListener("wheel", (e) => {
  if (e.defaultPrevented || e.deltaX === 0 || Math.abs(e.deltaX) < Math.abs(e.deltaY)) return;
  for (let n = e.target; n instanceof Element; n = n.parentElement) {
    if (n.scrollWidth <= n.clientWidth) continue;
    const ox = getComputedStyle(n).overflowX;
    if (ox !== "auto" && ox !== "scroll") continue;
    if (e.deltaX < 0 ? n.scrollLeft > 0 : n.scrollLeft + n.clientWidth < n.scrollWidth - 1) return;
  }
  e.preventDefault();
}, { passive: false });

function updateTopbar() {
  const t = DAW.single.Transport;
  if (t) {
    T.play.textContent = t.playing ? "❚❚" : "▶";
    T.play.classList.toggle("on", !!t.playing);
    if (!DAW.isPending(A + "Transport", A + "Transport")) {
      T.loop.set(t.loop);
      T.metro.set(t.metronome);
      if (!T.tempo.classList.contains("active")) T.tempo.set(t.tempo);
      if (!T.bpb.classList.contains("active")) T.bpb.set(t.beats_per_bar);
      if (!T.unit.classList.contains("active")) T.unit.set(t.beat_unit);
    }
  }
  const s = DAW.single.Settings;
  if (s && !DAW.isPending(A + "Settings", A + "Settings")) {
    if (!T.master.classList.contains("active")) T.master.set(s.master_volume);
    T.mute.set(s.mute);
  }
  const song = DAW.single.Song, ss = DAW.single.SongStatus, as = DAW.single.SongFile;
  const state = ss ? UI.enumLabel(ss.state) : "";
  T.songTitle.textContent = song && song.title ? song.title : (state === "None" ? "no song" : state);
  T.songTitle.title = ss && ss.error ? ss.error : (ss ? `${ss.state} ${ss.file || ""}` : "");
  T.songTitle.style.color = ss && ss.state === "SongFailed" ? "var(--red)" : ss && ss.state === "SongLoading" ? "var(--yellow)" : "var(--fg0)";
  if (as && document.activeElement !== T.songFile) T.songFile.value = as.file || "";
  if (as && as.file) {
    const known = JSON.parse(localStorage.getItem("daw.songs") || "[]");
    if (!known.includes(as.file)) {
      known.push(as.file);
      localStorage.setItem("daw.songs", JSON.stringify(known.slice(-30)));
    }
  }
  const files = JSON.parse(localStorage.getItem("daw.songs") || "[]");
  if (T.songList.childNodes.length !== files.length) {
    T.songList.textContent = "";
    for (const f of files) T.songList.appendChild(UI.el("option", { value: f }));
  }
}

function updateStatus() {
  const st = DAW.single.Status;
  if (!st) { T.status.textContent = DAW.conn ? (DAW.connected ? "no audio" : "offline") : ""; return; }
  T.status.innerHTML = "";
  T.status.append(
    UI.el("b", {}, UI.enumLabel(st.state)), ` ${(st.sample_rate / 1000).toFixed(1)}k  voices `,
    UI.el("b", {}, String(st.voices)), "  load ", UI.el("b", {}, `${(st.load * 100).toFixed(0)}%`),
    `  buses ${st.buses}`);
  T.status.title = `stolen ${st.voices_stolen}, refused ${st.voices_refused}, clock ${st.clock}`;
}

function updatePosition() {
  const t = DAW.single.Transport;
  if (!t) { T.pos.textContent = "-"; return; }
  const pos = DAW.position();
  const bar = t.beats_per_bar * 4 / (t.beat_unit || 4);
  const beatLen = 4 / (t.beat_unit || 4);
  const b = Math.floor(pos / bar);
  const inBar = pos - b * bar;
  const beat = Math.floor(inBar / beatLen);
  const six = Math.floor((inBar - beat * beatLen) / 0.25);
  T.pos.textContent = `${b + 1}.${beat + 1}.${six + 1}`;
  T.pos.title = `${pos.toFixed(2)} beats`;
}

const appSel = () => document.getElementById("app-select");

function updateApps(hosts) {
  const sel = appSel();
  const cur = DAW.host;
  const list = hosts.slice();
  if (cur && !list.some((h) => h.host === cur)) list.push({ host: cur, name: undefined, port: cur.split(":")[1], gone: true });
  sel.textContent = "";
  if (!list.length) sel.appendChild(UI.el("option", { value: "" }, "no apps found"));
  for (const h of list) {
    sel.appendChild(UI.el("option", { value: h.host }, `${h.name || "flecs app"} :${h.port}${h.gone ? " (gone)" : ""}`));
  }
  sel.value = cur || "";
  if (!cur && hosts.length) {
    const saved = localStorage.getItem("daw.host");
    const pick = hosts.find((h) => h.host === saved) || hosts[0];
    DAW.connect(pick.host);
    sel.value = pick.host;
  }
}

function updateDot() {
  const dot = document.getElementById("conn-dot");
  dot.className = "dot " + (!DAW.conn ? "" : !DAW.connected ? "wait" : DAW.hasAudio ? "ok" : "bad");
  dot.title = !DAW.conn ? "not connected" : !DAW.connected ? "connecting" : DAW.hasAudio ? "connected" : "no box.audio";
}

function setupLayout() {
  document.querySelectorAll("#tabs button").forEach((b) => {
    b.addEventListener("click", () => {
      DAW.view = b.dataset.view;
      localStorage.setItem("daw.view", DAW.view);
      document.querySelectorAll("#tabs button").forEach((x) => x.classList.toggle("active", x === b));
      V.renderAll();
    });
  });
  const saved = localStorage.getItem("daw.view");
  if (saved) {
    DAW.view = saved;
    document.querySelectorAll("#tabs button").forEach((x) => x.classList.toggle("active", x.dataset.view === saved));
  }
  const grip = document.getElementById("side-grip");
  const sideW = localStorage.getItem("daw.side.w");
  if (sideW) document.body.style.setProperty("--side-w", sideW + "px");
  grip.addEventListener("pointerdown", (e) => {
    e.preventDefault();
    grip.setPointerCapture(e.pointerId);
    grip.classList.add("active");
    const startX = e.clientX, startW = document.getElementById("detail").offsetWidth;
    const move = (ev) => {
      const w = Math.max(280, Math.min(window.innerWidth * 0.7, startW + startX - ev.clientX));
      document.body.style.setProperty("--side-w", w + "px");
      localStorage.setItem("daw.side.w", Math.round(w));
    };
    const up = () => {
      grip.classList.remove("active");
      grip.removeEventListener("pointermove", move);
      grip.removeEventListener("pointerup", up);
    };
    grip.addEventListener("pointermove", move);
    grip.addEventListener("pointerup", up);
  });
  appSel().addEventListener("change", () => { if (appSel().value) DAW.connect(appSel().value); });
  const hostInput = document.getElementById("app-host");
  hostInput.addEventListener("keydown", (e) => {
    e.stopPropagation();
    if (e.key === "Enter" && hostInput.value.trim()) {
      DAW.connect(flecs.normalizeHost(hostInput.value));
      hostInput.value = "";
      hostInput.blur();
      if (DAW.discovery) DAW.discovery.scan();
    }
  });
  document.addEventListener("keydown", (e) => {
    const t = e.target;
    if (t && (t.tagName === "INPUT" || t.tagName === "SELECT")) return;
    if (e.code === "Space") {
      e.preventDefault();
      if (DAW.single.Transport) DAW.transport({ playing: !DAW.single.Transport.playing });
    } else if (e.key === "Home") {
      DAW.seek(0);
    } else if (e.key === "Escape" && !DAW.detailPath && DAW.view === "graph") {
      G.clearFocus();
    }
  });
}

let lastSel = null;

DAW.on((what) => {
  if (what === "structure" || what === "select" || what === "connection") {
    if (what === "connection") updateDot();
    if (what === "select") {
      if (DAW.sel !== lastSel) document.getElementById("detail").scrollTop = 0;
      lastSel = DAW.sel;
      V.renderSelection();
      return;
    }
    if (DAW.dragging > 0 || UI.openMenu) DAW.needRender = true;
    else V.renderAll();
    updateTopbar();
  } else if (what === "values") {
    UI.refresh();
    updateTopbar();
    updateDot();
  } else if (what === "status") {
    updateStatus();
  } else if (what === "fast") {
    updateTopbar();
  }
});

function frame() {
  if (DAW.needRender && DAW.dragging <= 0 && !UI.openMenu) V.renderAll();
  UI.drawMeters();
  ED.drawAll();
  V.updateGr();
  updatePosition();
  requestAnimationFrame(frame);
}

if (DAW.detailPath) {
  document.body.classList.add("detail-mode");
  document.title = `${DAW.leaf(DAW.detailPath)} - box daw`;
  DAW.auditionPath = "daw_audition_" + Math.random().toString(36).slice(2, 8);
}
buildTopbar();
setupLayout();
V.renderAll();
updateDot();
DAW.autoMeter = true;
const urlHost = new URLSearchParams(location.search).get("host");
if (urlHost) DAW.connect(flecs.normalizeHost(urlHost));
if (!DAW.detailPath) DAW.discovery = flecs.startDiscovery({ interval_ms: 4000, on_hosts: updateApps });
requestAnimationFrame(frame);
