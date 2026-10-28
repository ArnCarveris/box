#!/usr/bin/env node
import { execFileSync } from 'node:child_process';
import { existsSync, readdirSync, readFileSync, statSync } from 'node:fs';
import { homedir } from 'node:os';
import { basename, dirname, join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const PACKAGE = 'webgpu@0.6.2';
const HOME = process.env.BOX_WGSL_HOME || join(homedir(), '.cache', 'box', 'wgsl');
const ENTRY = join(HOME, 'node_modules', 'webgpu', 'index.js');
const DEFAULT_DIR = 'etc/shaders';
const keep = {};
// Variants of a shader the engine compiles besides the file as is: the define it adds and the
// device feature the variant needs, requested from the validator when its adapter has it.
const VARIANTS = {
  'shadow.wgsl': [{ define: 'BOX_SHADOW_CLIP_DISTANCES', feature: 'clip-distances' }],
};
const FEATURES = [...new Set(Object.values(VARIANTS).flat().map((v) => v.feature))];
const USAGE = `Validates WGSL with Tint, the browser's compiler, through Dawn's node binding.

  node tools/wgsl_check.mjs --install          install the validator when it is missing
  node tools/wgsl_check.mjs [-D NAME[=v]] [<file|dir>...]
                                               preprocess and validate files; a directory
                                               means its *.wgsl, no path means ${DEFAULT_DIR}
  node tools/wgsl_check.mjs -                  validate "name\\0source\\0" records from stdin

Files go through the engine's preprocessor rules (#include "file", #pragma once, #define,
#undef, #ifdef, #ifndef, #else, #endif) before Tint sees them, and errors point at the
original file and line. A file the engine also compiles with a define (shadow.wgsl with
BOX_SHADOW_CLIP_DISTANCES) is validated in both variants; a variant that needs a device feature
(clip-distances) is skipped, and reported, when the validator's adapter lacks it. Tint runs the WGSL uniformity analysis that native wgpu skips. The
validator is installed in $BOX_WGSL_HOME, ~/.cache/box/wgsl when unset, which every worktree
shares. Exits 0 when every shader compiles, 1 when one fails and 2 when the validator cannot
run.`;

function install() {
  if (!existsSync(ENTRY)) {
    console.log(`installing ${PACKAGE} into ${HOME}`);
    execFileSync('npm', ['install', '--prefix', HOME, '--no-audit', '--no-fund', PACKAGE],
      { stdio: 'inherit' });
  }
  console.log(`validator: ${ENTRY}`);
}

function parseDefine(text, defines) {
  const eq = text.indexOf('=');
  if (eq < 0) {
    defines.set(text.trim(), '');
  } else {
    defines.set(text.slice(0, eq).trim(), text.slice(eq + 1).trim());
  }
}

function expand(line, defines) {
  if (!defines.size) {
    return line;
  }
  let result = line;
  for (let depth = 0; depth < 16; depth++) {
    const next = result.replace(/[A-Za-z_][A-Za-z0-9_]*/g,
      (name) => (defines.has(name) ? defines.get(name) : name));
    if (next === result) {
      break;
    }
    result = next;
  }
  return result;
}

function locate(from, path) {
  const candidates = [join(dirname(from), path)];
  for (let dir = process.cwd(); ; dir = dirname(dir)) {
    candidates.push(join(dir, path));
    if (dirname(dir) === dir) {
      break;
    }
  }
  return candidates.find((c) => existsSync(c) && statSync(c).isFile());
}

function preprocess(file, defines) {
  const state = { lines: [], origin: [], once: new Set(), error: null };
  include(state, resolve(file), defines, 0);
  return state;
}

function include(state, file, defines, depth) {
  if (state.once.has(file)) {
    return;
  }
  if (depth > 32) {
    state.error = `${file}: includes nested too deep`;
    return;
  }
  const source = readFileSync(file, 'utf8').split('\n');
  const conds = [];
  for (let i = 0; i < source.length && !state.error; i++) {
    const text = source[i];
    const active = conds.every((c) => c);
    const m = /^\s*#\s*(\w+)\s*(.*?)\s*$/.exec(text);
    if (!m) {
      if (active) {
        state.lines.push(expand(text, defines));
        state.origin.push({ file, line: i + 1 });
      }
      continue;
    }
    const [, directive, arg] = m;
    const where = `${file}:${i + 1}`;
    if (directive === 'ifdef' || directive === 'ifndef') {
      conds.push(defines.has(arg) === (directive === 'ifdef'));
    } else if (directive === 'else') {
      if (!conds.length) {
        state.error = `${where}: #else without #ifdef`;
      } else {
        conds.push(!conds.pop());
      }
    } else if (directive === 'endif') {
      if (!conds.length) {
        state.error = `${where}: #endif without #ifdef`;
      } else {
        conds.pop();
      }
    } else if (!active) {
      continue;
    } else if (directive === 'define') {
      const d = /^(\w+)\s*(.*)$/.exec(arg);
      if (d) {
        defines.set(d[1], d[2]);
      } else {
        state.error = `${where}: #define expects a macro name`;
      }
    } else if (directive === 'undef') {
      defines.delete(arg);
    } else if (directive === 'pragma' && arg === 'once') {
      state.once.add(file);
    } else if (directive === 'include') {
      const name = /^"(.+)"$/.exec(arg);
      const path = name ? locate(file, name[1]) : null;
      if (!path) {
        state.error = `${where}: cannot include ${arg}`;
      } else {
        include(state, resolve(path), defines, depth + 1);
      }
    } else {
      state.error = `${where}: unknown directive #${directive}`;
    }
  }
  if (!state.error && conds.length) {
    state.error = `${file}: #ifdef without #endif`;
  }
}

function readInputs(args) {
  const shaders = [];
  const defines = new Map();
  const paths = [];
  let stdin = false;
  for (let i = 0; i < args.length; i++) {
    if (args[i] === '-D') {
      parseDefine(args[++i] || '', defines);
    } else if (args[i].startsWith('-D')) {
      parseDefine(args[i].slice(2), defines);
    } else if (args[i] === '-') {
      stdin = true;
    } else {
      paths.push(args[i]);
    }
  }
  if (stdin) {
    const parts = readFileSync(0, 'utf8').split('\0');
    for (let i = 0; i + 1 < parts.length; i += 2) {
      const lines = parts[i + 1].split('\n');
      shaders.push({ name: parts[i], code: parts[i + 1],
        origin: lines.map((_, n) => ({ file: parts[i], line: n + 1 })) });
    }
    return shaders;
  }
  for (const arg of paths.length ? paths : [DEFAULT_DIR]) {
    const files = statSync(arg).isDirectory()
      ? readdirSync(arg).filter((f) => f.endsWith('.wgsl')).sort().map((f) => join(arg, f))
      : [arg];
    for (const file of files) {
      const out = preprocess(file, new Map(defines));
      shaders.push({ name: file, code: out.lines.join('\n'), origin: out.origin,
        error: out.error });
      for (const variant of VARIANTS[basename(file)] || []) {
        const variantDefines = new Map(defines);
        variantDefines.set(variant.define, '');
        const v = preprocess(file, variantDefines);
        shaders.push({ name: `${file} -D ${variant.define}`, code: v.lines.join('\n'),
          origin: v.origin, error: v.error, feature: variant.feature });
      }
    }
  }
  return shaders;
}

async function openDevice() {
  if (!existsSync(ENTRY)) {
    console.log(`wgsl_check: ${PACKAGE} is not installed in ${HOME}, ` +
      'run node tools/wgsl_check.mjs --install');
    return null;
  }
  const { create, globals } = await import(pathToFileURL(ENTRY).href);
  Object.assign(globalThis, globals);
  keep.gpu = create([]);
  const adapter = await keep.gpu.requestAdapter();
  if (!adapter) {
    console.log('wgsl_check: no WebGPU adapter');
    return null;
  }
  const requiredFeatures = FEATURES.filter((f) => adapter.features.has(f));
  return adapter.requestDevice({ requiredFeatures });
}

async function check(device, shader) {
  if (shader.error) {
    console.log(`${shader.error}`);
    return 1;
  }
  device.pushErrorScope('validation');
  const module = device.createShaderModule({ label: shader.name, code: shader.code });
  const info = await module.getCompilationInfo();
  await device.popErrorScope();
  const lines = shader.code.split('\n');
  let errors = 0;
  for (const m of info.messages) {
    if (m.type === 'warning') {
      continue;
    }
    errors += m.type === 'error' ? 1 : 0;
    const at = shader.origin[m.lineNum - 1] || { file: shader.name, line: m.lineNum };
    console.log(`${at.file}:${at.line}:${m.linePos}: ${m.type}: ${m.message}`);
    if (m.type === 'error' && m.lineNum > 0) {
      console.log(`    ${lines[m.lineNum - 1].trim()}`);
    }
  }
  return errors;
}

async function main() {
  const args = process.argv.slice(2);
  if (args[0] === '--help' || args[0] === '-h') {
    console.log(USAGE);
    return 0;
  }
  if (args[0] === '--install') {
    install();
    return 0;
  }
  const shaders = readInputs(args);
  const device = await openDevice();
  if (!device) {
    return 2;
  }
  let failed = 0;
  let skipped = 0;
  for (const shader of shaders) {
    if (shader.feature && !device.features.has(shader.feature)) {
      console.log(`skip ${shader.name}: the validator lacks the ${shader.feature} feature`);
      skipped++;
      continue;
    }
    const errors = await check(device, shader);
    failed += errors ? 1 : 0;
    if (!errors) {
      console.log(`ok ${shader.name}`);
    }
  }
  device.destroy();
  const checked = shaders.length - skipped;
  console.log(`wgsl_check: ${checked - failed} of ${checked} shader(s) passed` +
    (skipped ? `, ${skipped} skipped` : ''));
  return failed ? 1 : 0;
}

process.exit(await main());
