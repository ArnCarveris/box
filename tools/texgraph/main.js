"use strict";

// The top bar (app discovery and the connection dot as in tools/daw/main.js, the texture
// picker and the script export), the statistics bar and the wiring of the views to TG events.

const T = {};

const appSel = () => document.getElementById("app-select");

function updateApps(hosts) {
  const sel = appSel();
  const cur = TG.host;
  const list = hosts.slice();
  if (cur && !list.some((h) => h.host === cur)) list.push({ host: cur, name: undefined, port: cur.split(":")[1], gone: true });
  sel.textContent = "";
  if (!list.length) sel.appendChild(G.el("option", { value: "" }, "no apps found"));
  for (const h of list) {
    sel.appendChild(G.el("option", { value: h.host }, `${h.name || "flecs app"} :${h.port}${h.gone ? " (gone)" : ""}`));
  }
  sel.value = cur || "";
  if (!cur && hosts.length) {
    const saved = localStorage.getItem("texgraph.host");
    const pick = hosts.find((h) => h.host === saved) || hosts[0];
    TG.connect(pick.host);
    sel.value = pick.host;
  }
}

function updateDot() {
  const dot = document.getElementById("conn-dot");
  dot.className = "dot " + (!TG.conn ? "" : !TG.connected ? "wait" : TG.hasTexture ? "ok" : "bad");
  dot.title = !TG.conn ? "not connected" : !TG.connected ? "connecting" : TG.hasTexture ? "connected" : "no box.texture";
}

function updateTextures() {
  const sel = T.tex;
  const paths = Object.keys(TG.textures).sort();
  const key = paths.join("|") + "#" + (TG.tex || "");
  if (T.texKey !== key) {
    T.texKey = key;
    sel.textContent = "";
    if (!paths.length) sel.appendChild(G.el("option", { value: "" }, TG.connected ? "no texture graphs" : "-"));
    for (const p of paths) sel.appendChild(G.el("option", { value: p }, p));
    sel.value = TG.tex || "";
  }
  const tex = TG.tex ? TG.textures[TG.tex] : null;
  const st = tex && tex.status ? String(tex.status.state || "").replace(/^Texture/, "") : "";
  T.texState.textContent = st;
  T.texState.className = "state s-" + st.toLowerCase();
  T.texState.title = tex && tex.status && tex.status.error ? tex.status.error : "";
  T.script.disabled = !tex;
}

const fmtInt = (v) => v === undefined || v === null ? "-" : Number(v).toLocaleString("en-US");

const fmtBytes = (v) => !v ? "0" : v >= 1 << 20 ? (v / (1 << 20)).toFixed(1) + " MB" : (v / 1024).toFixed(0) + " KB";

function updateStats() {
  const s = TG.stats;
  const bar = T.stats;
  bar.textContent = "";
  if (!s) {
    bar.append(TG.connected ? "no graph.Stats (the app does not render with the GPU backend)" : "");
    return;
  }
  const r = TG.statsRates();
  const item = (label, value, title) => G.el("span", { class: "stat", title }, label + " ", G.el("b", {}, value));
  bar.append(
    item("graphs", fmtInt(s.graphs), "texture graphs alive on the backend"),
    item("programs", fmtInt(s.programs), "generated stage programs, shared by graphs of one structure"),
    item("evaluations", fmtInt(s.evaluations) + (r ? ` (${r.evaluations.toFixed(1)}/s)` : ""), "evaluations of texture graphs recorded"),
    item("dispatches", fmtInt(s.dispatches), "stage dispatches recorded"),
    item("texels", fmtInt(s.texels), "texels the stage dispatches computed"),
    item("gpu", s.timed ? (r && r.gpu_ms !== null ? r.gpu_ms.toFixed(3) : (s.gpu_ms / s.timed).toFixed(3)) + " ms" : "untimed",
      "GPU time per measured packet of evaluations"),
    item("pool", fmtBytes(s.pool_bytes), "pooled intermediate textures"),
    item("reads", fmtInt(s.reads) + (r ? ` (${r.reads.toFixed(1)}/s)` : ""), "readbacks delivered, previews included"),
    item("previews", fmtInt(s.previews), "nodes with a graph.Preview"));
}

function buildTopbar() {
  T.tex = document.getElementById("tex-select");
  T.tex.addEventListener("change", () => { if (T.tex.value) TG.selectTexture(T.tex.value); });
  T.texState = document.getElementById("tex-state");
  T.script = document.getElementById("copy-script");
  T.script.addEventListener("click", () => P.scriptDialog());
  T.stats = document.getElementById("stats");
}

function setupLayout() {
  const grip = document.getElementById("side-grip");
  const sideW = localStorage.getItem("texgraph.side.w");
  if (sideW) document.body.style.setProperty("--side-w", sideW + "px");
  grip.addEventListener("pointerdown", (e) => {
    e.preventDefault();
    grip.setPointerCapture(e.pointerId);
    grip.classList.add("active");
    const startX = e.clientX, startW = document.getElementById("detail").offsetWidth;
    const move = (ev) => {
      const w = Math.max(280, Math.min(window.innerWidth * 0.7, startW + startX - ev.clientX));
      document.body.style.setProperty("--side-w", w + "px");
      localStorage.setItem("texgraph.side.w", Math.round(w));
    };
    const up = () => {
      grip.classList.remove("active");
      grip.removeEventListener("pointermove", move);
      grip.removeEventListener("pointerup", up);
      G.schedulePreviews();
    };
    grip.addEventListener("pointermove", move);
    grip.addEventListener("pointerup", up);
  });
  appSel().addEventListener("change", () => { if (appSel().value) TG.connect(appSel().value); });
  const hostInput = document.getElementById("app-host");
  hostInput.addEventListener("keydown", (e) => {
    e.stopPropagation();
    if (e.key === "Enter" && hostInput.value.trim()) {
      TG.connect(flecs.normalizeHost(hostInput.value));
      hostInput.value = "";
      hostInput.blur();
      if (TG.discovery) TG.discovery.scan();
    }
  });
  document.addEventListener("keydown", (e) => {
    const t = e.target;
    if (t && (t.tagName === "INPUT" || t.tagName === "SELECT" || t.tagName === "TEXTAREA")) return;
    if (e.key === "Delete" || e.key === "Backspace") {
      e.preventDefault();
      G.deleteSelection();
    } else if (e.key === "Escape") {
      G.closeMenu();
      G.picked.clear();
      G.applyPicked();
      G.selEdge = null;
      if (TG.sel) TG.select(null);
    } else if (e.key === "f" && !e.metaKey && !e.ctrlKey) {
      G.fit();
    }
  });
  window.addEventListener("resize", () => G.schedulePreviews());
}

function renderAll() {
  G.renderAll();
  P.render();
}

TG.on((what, arg) => {
  if (what === "connection") {
    updateDot();
    updateTextures();
    updateStats();
  } else if (what === "structure") {
    if (TG.dragging > 0 || G.openMenu) TG.needRender = true;
    else renderAll();
    updateTextures();
    updateDot();
  } else if (what === "values") {
    G.refreshValues();
    if (P.shown !== (TG.sel || TG.tex)) P.render();
    else P.refresh();
    updateTextures();
    updateDot();
  } else if (what === "select") {
    G.markSelected();
    P.render();
    G.schedulePreviews();
  } else if (what === "previews") {
    G.showPreviews(arg);
    P.refresh();
  } else if (what === "stats") {
    updateStats();
  }
});

function frame() {
  if (TG.needRender && TG.dragging <= 0 && !G.openMenu) renderAll();
  requestAnimationFrame(frame);
}

buildTopbar();
setupLayout();
renderAll();
updateDot();
updateTextures();
const urlHost = new URLSearchParams(location.search).get("host");
if (urlHost) TG.connect(flecs.normalizeHost(urlHost));
TG.discovery = flecs.startDiscovery({ interval_ms: 4000, on_hosts: updateApps });
requestAnimationFrame(frame);
