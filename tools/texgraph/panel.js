"use strict";

// The side panel: the selected node with its large preview, inputs and parameters edited from
// the reflected type of its component, or the Graph of the texture with its status when
// no node is selected. Edits are written live with TG.write.

const P = window.P = { refreshers: [] };

const el = (...args) => G.el(...args);

P.dragNum = (opts) => {
  let value = opts.value;
  const node = el("div", { class: "drag-num" + (opts.cls ? " " + opts.cls : ""), title: opts.doc || "drag or click to type" });
  const fmt = (v) => v === undefined || v === null ? "-" : opts.int ? String(Math.round(v)) : TGM.num(+(+v).toFixed(4));
  const draw = () => { if (!node.querySelector("input")) node.textContent = (opts.label ? opts.label + " " : "") + fmt(value); };
  let startY = 0, startV = 0, moved = false;
  node.addEventListener("pointerdown", (e) => {
    if (e.target.tagName === "INPUT") return;
    node.setPointerCapture(e.pointerId);
    node.classList.add("active");
    TG.dragging++;
    startY = e.clientY;
    startV = +value || 0;
    moved = false;
  });
  node.addEventListener("pointermove", (e) => {
    if (!node.classList.contains("active")) return;
    const dy = startY - e.clientY;
    if (Math.abs(dy) > 2) moved = true;
    if (!moved) return;
    const step = opts.step || (opts.int ? 0.2 : 0.005);
    let v = startV + dy * step * (e.shiftKey ? 0.1 : 1);
    if (opts.min !== undefined) v = Math.max(opts.min, v);
    if (opts.max !== undefined) v = Math.min(opts.max, v);
    v = opts.int ? Math.round(v) : Math.round(v * 10000) / 10000;
    if (v !== value) { value = v; draw(); opts.onchange(v); }
  });
  const end = () => {
    if (!node.classList.contains("active")) return;
    node.classList.remove("active");
    TG.dragging--;
    if (moved) return;
    const input = el("input", { value: String(value === undefined ? 0 : value) });
    node.textContent = "";
    node.appendChild(input);
    input.focus();
    input.select();
    const done = (apply) => {
      if (!input.parentNode) return;
      const v = parseFloat(input.value);
      node.removeChild(input);
      if (apply && !Number.isNaN(v) && v !== value) { value = opts.int ? Math.round(v) : v; opts.onchange(value); }
      draw();
    };
    input.addEventListener("keydown", (ev) => {
      ev.stopPropagation();
      if (ev.key === "Enter") done(true);
      if (ev.key === "Escape") done(false);
    });
    input.addEventListener("blur", () => done(true));
  };
  node.addEventListener("pointerup", end);
  node.addEventListener("pointercancel", end);
  node.set = (v) => { if (v !== value && !node.classList.contains("active")) { value = v; draw(); } };
  draw();
  return node;
};

P.zero = (ti) => {
  if (Array.isArray(ti)) {
    const k = ti[0];
    if (k === "float" || k === "int" || k === "byte") return 0;
    if (k === "bool") return false;
    if (k === "enum") return ti[1];
    if (k === "entity") return "#0";
    if (k === "text") return "";
    if (k === "vector") return [];
    if (k === "array") return Array.from({ length: ti[2] || 0 }, () => P.zero(ti[1]));
    return 0;
  }
  if (ti && typeof ti === "object") {
    const v = {};
    for (const k of Object.keys(ti)) if (k !== "@self") v[k] = P.zero(ti[k]);
    return v;
  }
  return 0;
};

P.structKeys = (ti) => ti && typeof ti === "object" && !Array.isArray(ti) ? Object.keys(ti).filter((k) => k !== "@self") : [];

P.isRgb = (ti) => {
  const k = P.structKeys(ti);
  return (k.join() === "r,g,b" || k.join() === "r,g,b,a") && k.every((x) => ti[x][0] === "int" || ti[x][0] === "byte");
};

P.isVec = (ti) => {
  const k = P.structKeys(ti);
  return ["x,y", "x,y,z", "x,y,z,w"].includes(k.join()) && k.every((x) => ti[x][0] === "float");
};

P.isRamp = (ti) => Array.isArray(ti) && ti[0] === "vector" && P.structKeys(ti[1]).includes("position") &&
  P.structKeys(ti[1]).includes("color");

// Options of an entity reference: no entity, then the nodes of the graph and the textures.
P.entityOptions = (filter) => {
  const opts = [{ value: "#0", label: "(none)" }];
  const g = TG.graph();
  const seen = new Set();
  for (const path of [...g.nodes.keys(), ...Object.keys(TG.nodes).sort(), ...Object.keys(TG.textures).sort()]) {
    if (seen.has(path)) continue;
    seen.add(path);
    const why = filter ? filter(path) : null;
    opts.push({ value: path, label: TGM.under(path, TG.tex) ? path.slice(TG.tex.length + 1) : path, disabled: !!why, title: why || path });
  }
  return opts;
};

P.select = (opts, value, onchange) => {
  const s = el("select", { onchange: () => onchange(s.value) });
  for (const o of opts) s.appendChild(el("option", { value: o.value, disabled: o.disabled, title: o.title }, o.label));
  s.set = (v) => {
    if (document.activeElement === s) return;
    const want = TGM.isNone(v) ? "#0" : String(v);
    if (![...s.options].some((o) => o.value === want)) s.appendChild(el("option", { value: want }, want + " (missing)"));
    s.value = want;
  };
  s.set(value);
  return s;
};

P.colorInput = (rgb, onchange) => {
  const c = el("input", { type: "color", value: TGM.hex(rgb[0], rgb[1], rgb[2]) });
  c.addEventListener("input", () => onchange(TGM.unhex(c.value)));
  c.set = (v) => { if (document.activeElement !== c) c.value = TGM.hex(v[0], v[1], v[2]); };
  return c;
};

// An editor of a value of a reflected type. onchange gets the whole new value; the editor has
// set(value) to show a value that changed in the app.
P.editor = (ti, value, onchange, label) => {
  const kind = Array.isArray(ti) ? ti[0] : (ti && typeof ti === "object" ? "struct" : null);
  let cur = TG.clone(value);
  const change = (v) => { cur = v; onchange(TG.clone(v)); };
  let node;
  if (kind === "float" || kind === "int" || kind === "byte") {
    node = P.dragNum({ value: cur, int: kind !== "float", onchange: change, label });
  } else if (kind === "bool") {
    node = el("input", { type: "checkbox", onchange: () => change(node.checked) });
    node.checked = !!cur;
    node.set = (v) => { node.checked = !!v; };
  } else if (kind === "enum") {
    node = P.select(ti.slice(1).map((x) => ({ value: x, label: x })), cur, change);
  } else if (kind === "entity") {
    node = P.select(P.entityOptions(), cur, change);
  } else if (kind === "text") {
    node = el("input", { value: cur || "", onchange: () => change(node.value) });
    node.addEventListener("keydown", (e) => e.stopPropagation());
    node.set = (v) => { if (document.activeElement !== node) node.value = v || ""; };
  } else if (kind === "struct" && P.isRgb(ti)) {
    const v0 = cur || {};
    const color = P.colorInput([v0.r || 0, v0.g || 0, v0.b || 0], (rgb) => change(Object.assign({}, cur, { r: rgb[0], g: rgb[1], b: rgb[2] })));
    node = el("span", { class: "row" }, color);
    let alpha = null;
    if (P.structKeys(ti).includes("a")) {
      alpha = P.dragNum({ value: v0.a, int: true, min: 0, max: 255, label: "a", onchange: (a) => change(Object.assign({}, cur, { a })) });
      node.appendChild(alpha);
    }
    node.set = (v) => {
      v = v || {};
      cur = TG.clone(v);
      color.set([v.r || 0, v.g || 0, v.b || 0]);
      if (alpha) alpha.set(v.a);
    };
  } else if (kind === "struct" && P.isVec(ti)) {
    const keys = P.structKeys(ti);
    const nums = keys.map((k) => P.dragNum({ value: (cur || {})[k], label: k, cls: "vec", onchange: (x) => change(Object.assign({}, cur, { [k]: x })) }));
    const toRgb = (v) => ["x", "y", "z"].map((k) => 255 * TGM.toSrgb((v || {})[k] || 0));
    const color = keys.length >= 3 ? P.colorInput(toRgb(cur), (rgb) => change(Object.assign({}, cur, {
      x: +TGM.fromSrgb(rgb[0] / 255).toFixed(4), y: +TGM.fromSrgb(rgb[1] / 255).toFixed(4), z: +TGM.fromSrgb(rgb[2] / 255).toFixed(4)
    }))) : null;
    if (color) color.title = "pick as a linear colour (x, y, z)";
    node = el("span", { class: "row vec" }, nums, color);
    node.set = (v) => {
      cur = TG.clone(v);
      keys.forEach((k, i) => nums[i].set((v || {})[k]));
      if (color) color.set(toRgb(v));
    };
  } else if (kind === "struct") {
    const keys = P.structKeys(ti);
    const subs = {};
    node = el("div", { class: "nested" });
    for (const k of keys) {
      subs[k] = P.editor(ti[k], (cur || {})[k], (x) => change(Object.assign({}, cur, { [k]: x })));
      node.appendChild(el("div", { class: "field" }, el("label", {}, k.replace(/_/g, " ")), subs[k]));
    }
    node.set = (v) => {
      cur = TG.clone(v);
      for (const k of keys) subs[k].set((v || {})[k]);
    };
  } else if (kind === "array" || kind === "vector") {
    node = P.list(ti, cur, change);
  } else {
    node = el("code", {}, JSON.stringify(cur));
    node.set = (v) => { node.textContent = JSON.stringify(v); };
  }
  return node;
};

// An editor of an array or vector: a row per element, and for a vector add and remove; a
// colour ramp (elements with a position and a colour) also shows its gradient.
P.list = (ti, value, onchange) => {
  const vector = ti[0] === "vector";
  const node = el("div", { class: "list" });
  let cur = TG.clone(value) || [];
  const draw = () => {
    node.textContent = "";
    if (P.isRamp(ti)) node.appendChild(P.ramp(cur, ti[1]));
    cur.forEach((item, i) => {
      const ed = P.editor(ti[1], item, (x) => {
        cur = cur.slice();
        cur[i] = x;
        onchange(cur);
        if (P.isRamp(ti)) draw();
      });
      const row = el("div", { class: "lrow" }, el("span", { class: "lidx" }, String(i)), ed);
      if (vector) {
        row.appendChild(el("button", {
          class: "small", text: "×", title: "remove", onclick: () => {
            cur = cur.filter((_, j) => j !== i);
            onchange(cur);
            draw();
          }
        }));
      }
      node.appendChild(row);
    });
    if (vector) {
      node.appendChild(el("button", {
        class: "small", text: "+ add", onclick: () => {
          const last = cur.length ? TG.clone(cur[cur.length - 1]) : P.zero(ti[1]);
          cur = cur.concat([last]);
          onchange(cur);
          draw();
        }
      }));
    }
  };
  draw();
  node.set = (v) => {
    if (node.contains(document.activeElement) || TG.dragging > 0) return;
    if (JSON.stringify(v) === JSON.stringify(cur)) return;
    cur = TG.clone(v) || [];
    draw();
  };
  return node;
};

P.ramp = (keys, ti) => {
  const c = el("canvas", { class: "ramp", width: 256, height: 14 });
  const ctx = c.getContext("2d");
  const grad = ctx.createLinearGradient(0, 0, 256, 0);
  let pos = 0;
  const vec = P.isVec(ti.color);
  for (const k of keys || []) {
    pos = Math.max(pos, Math.min(1, +k.position || 0));
    const col = k.color || {};
    const rgb = vec ? ["x", "y", "z"].map((x) => 255 * TGM.toSrgb(col[x] || 0)) : [col.r || 0, col.g || 0, col.b || 0];
    grad.addColorStop(pos, TGM.hex(rgb[0], rgb[1], rgb[2]));
  }
  ctx.fillStyle = keys && keys.length ? grad : "#000";
  ctx.fillRect(0, 0, 256, 14);
  return c;
};

P.section = (title, ...children) => el("section", { class: "psec" }, el("h3", {}, title), ...children);

P.field = (label, editor, doc) => el("div", { class: "field", title: doc }, el("label", {}, label), editor);

P.bindValue = (path, comp, member, editor) => {
  P.refreshers.push(() => {
    const v = TG.valueOf(path, comp);
    if (v && !TG.isPending(path, comp)) editor.set(v[member]);
  });
};

P.previewBlock = (path) => {
  const p = TG.previews[path];
  const tex = TG.textures[TG.tex];
  const sphere = tex && tex.graph && tex.graph.domain === "DomainSphere";
  const img = G.previewImg(path);
  img.classList.add("big");
  const caption = (q) => q ? `${q.width} × ${q.height}, revision ${q.revision}`
    : TGM.isReached(TG, path) ? "waiting for the preview"
      : "no preview: the output of no graph reaches this node";
  const cap = el("div", { class: "pcap" }, caption(p), sphere ? ", equirectangular" : "");
  P.refreshers.push(() => { cap.firstChild.textContent = caption(TG.previews[path]); });
  return el("div", { class: "pbig" + (sphere ? " sphere" : "") }, img, cap);
};

P.renderNode = (root, n) => {
  const kind = TGM.nodeKind(TG, n);
  const comp = n.kinds[0];
  const ti = TG.types[comp];
  const v = TGM.nodeValue(n) || {};
  const tex = TG.textures[TG.tex];
  const isOutput = tex && tex.graph && tex.graph.output === n.path;
  const errors = tex && tex.status && tex.status.error &&
    TGM.errorTargets(TG.graph(), tex.status.error).includes(n.path) ? tex.status.error : null;
  root.appendChild(el("div", { class: "phead" },
    el("span", { class: "swatch", style: { background: G.color(n.kinds.map(TGM.short).join(" + ") + (kind && kind.output === "Scalar" ? " · scalar" : "")) } }),
    el("div", {},
      el("div", { class: "ptitle" }, n.name, isOutput ? el("span", { class: "badge" }, "output") : null),
      el("div", { class: "psub", title: n.path }, n.path))));
  if (errors) root.appendChild(el("div", { class: "perror" }, errors));
  if (n.kinds.length > 1) root.appendChild(el("div", { class: "perror" }, `more than one node component: ${n.kinds.map(TGM.short).join(", ")}`));
  root.appendChild(P.previewBlock(n.path));
  root.appendChild(el("div", { class: "pbuttons" },
    el("button", { text: isOutput ? "is the graph output" : "set as graph output", disabled: isOutput || !kind || kind.output === "Scalar", onclick: () => TG.connectPorts(n.path, "@output", "output") }),
    el("button", { text: "delete", class: "danger", onclick: () => G.deleteNodes([n.path]) })));
  if (!kind) return;
  root.appendChild(el("div", { class: "pinfo" },
    `${TGM.short(comp)} · outputs ${TGM.typeLabel(kind.output)}`,
    kind.neighbourhood ? " · reads neighbours" : "", kind.animated ? " · animated" : "",
    el("span", { class: "dim" }, `  kind ${kind.id}, ${kind.fn}`)));

  const used = new Set();
  if (kind.inputs.length) {
    const rows = [];
    for (const p of kind.inputs) {
      used.add(p.name);
      if (p.value) used.add(p.value);
      const s = P.select(P.entityOptions((path) => path === n.path ? "itself" :
        !TG.nodes[path] ? "not a node" : TGM.connectError(TG, path, n.path, p.name)), v[p.name], (x) => {
        if (TGM.isNone(x)) TG.disconnectPort(n.path, p.name);
        else TG.connectPorts(x, n.path, p.name);
      });
      P.bindValue(n.path, comp, p.name, s);
      const row = el("div", { class: "pport" },
        el("span", { class: "port in " + G.typeClass(p.type) }),
        el("label", { title: `takes ${TGM.typeLabel(p.type)}` }, p.name), s);
      rows.push(row);
      if (p.value && ti && ti[p.value] && TGM.isNone(v[p.name])) {
        const ed = P.editor(ti[p.value], v[p.value], (x) => TG.write(n.path, comp, { [p.value]: x }));
        P.bindValue(n.path, comp, p.value, ed);
        rows.push(el("div", { class: "pfallback", title: `${p.value}: the value of ${p.name} without a node` }, ed));
      }
    }
    root.appendChild(P.section("inputs", ...rows));
  }

  const params = [];
  for (const k of P.structKeys(ti)) {
    if (used.has(k)) continue;
    const ed = P.editor(ti[k], v[k], (x) => TG.write(n.path, comp, { [k]: x }));
    P.bindValue(n.path, comp, k, ed);
    params.push(P.field(k.replace(/_/g, " "), ed));
  }
  if (params.length) root.appendChild(P.section("parameters", ...params));
  if (!ti) root.appendChild(el("div", { class: "dim" }, "waiting for the type of " + comp));
  for (const other of n.kinds.slice(1)) {
    const oti = TG.types[other];
    const ov = n.values[other] || {};
    const fields = P.structKeys(oti).map((k) => {
      const ed = P.editor(oti[k], ov[k], (x) => TG.write(n.path, other, { [k]: x }));
      P.bindValue(n.path, other, k, ed);
      return P.field(k, ed);
    });
    root.appendChild(P.section(TGM.short(other), ...fields));
  }
};

P.renderTexture = (root, tex) => {
  const comp = TEX + "Graph";
  const ti = TG.types[comp];
  const st = tex.status || {};
  root.appendChild(el("div", { class: "phead" },
    el("span", { class: "swatch", style: { background: "var(--accent)" } }),
    el("div", {}, el("div", { class: "ptitle" }, tex.name), el("div", { class: "psub", title: tex.path }, tex.path))));
  const state = st.state ? String(st.state).replace(/^Texture/, "") : "no status";
  root.appendChild(el("div", { class: "pinfo" },
    el("span", { class: "state s-" + state.toLowerCase() }, state),
    st.revision !== undefined ? ` revision ${st.revision}` : "",
    tex.size ? ` · ${tex.size.width} × ${tex.size.height}` : ""));
  if (st.error) root.appendChild(el("div", { class: "perror" }, st.error));
  if (tex.graph && !TGM.isNone(tex.graph.output)) root.appendChild(P.previewBlock(tex.graph.output));
  const fields = [];
  for (const k of P.structKeys(ti)) {
    let ed;
    if (k === "output") {
      ed = P.select(P.entityOptions((path) => !TG.nodes[path] ? "not a node" : TGM.connectError(TG, path, "@output", "output")),
        tex.graph[k], (x) => TGM.isNone(x) ? TG.disconnectPort("@output", "output") : TG.connectPorts(x, "@output", "output"));
    } else {
      ed = P.editor(ti[k], tex.graph[k], (x) => TG.write(tex.path, comp, { [k]: x }));
    }
    P.bindValue(tex.path, comp, k, ed);
    fields.push(P.field(k, ed));
  }
  if (!ti) fields.push(el("div", { class: "dim" }, "waiting for the type of Graph"));
  root.appendChild(P.section("Graph", ...fields));
  const g = TG.graph();
  root.appendChild(P.section(`${g.nodes.size} nodes`, ...[...g.nodes.values()].map((n) => el("div", {
    class: "plink", onclick: () => TG.select(n.path), title: n.path
  }, n.name, el("span", { class: "dim" }, " " + n.kinds.map(TGM.short).join(" + "))))));
  root.appendChild(el("div", { class: "pbuttons" },
    el("button", { text: "copy as script", onclick: () => P.scriptDialog() })));
};

P.render = () => {
  const root = document.getElementById("detail");
  if (!root) return;
  P.refreshers = [];
  root.textContent = "";
  const n = TG.sel ? TG.nodes[TG.sel] : null;
  const tex = TG.tex ? TG.textures[TG.tex] : null;
  if (n) P.renderNode(root, n);
  else if (tex) P.renderTexture(root, tex);
  else root.appendChild(el("div", { class: "dim pempty" }, TG.connected ? "pick a texture graph" : "connect to a box app"));
  P.shown = TG.sel || TG.tex;
};

P.refresh = () => {
  for (const fn of P.refreshers) fn();
};

P.scriptDialog = () => {
  if (!TG.tex) return;
  const text = TGM.toScript(TG, TG.tex);
  const area = el("textarea", { class: "script", spellcheck: "false", readonly: true });
  area.value = text;
  area.addEventListener("keydown", (e) => e.stopPropagation());
  const close = () => back.remove();
  const copy = () => {
    const done = () => TG.toast("copied the script of " + TGM.leaf(TG.tex));
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(text).then(done, () => { area.select(); document.execCommand("copy"); done(); });
    } else {
      area.select();
      document.execCommand("copy");
      done();
    }
  };
  const back = el("div", { class: "modal-back", onclick: (e) => { if (e.target === back) close(); } },
    el("div", { class: "modal" },
      el("h3", {}, "Script of " + TG.tex),
      el("div", { class: "warn" }, "Edits live only in the running app: they are lost when it reloads its scene script. Paste this into the scene file to keep them."),
      area,
      el("div", { class: "pbuttons" }, el("button", { text: "copy", onclick: copy }), el("button", { text: "close", onclick: close }))));
  document.body.appendChild(back);
  area.focus();
  area.select();
  copy();
};
