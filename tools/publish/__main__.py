import argparse
import hashlib
import html
import json
import os
import re
import shutil
import subprocess
import sys

TOOL_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(TOOL_DIR))
DEFAULT_SITE = os.path.normpath(os.path.join(ROOT, "..", "..", "SanderMertens.github.io"))
ENGINE_APP = "apps/boxweb"
ENGINE_NAME = "boxweb"
ENGINE_DIR = "box"
EMBEDDED = ("etc/shaders/", "etc/fonts/", "etc/luts/", "etc/environments/",
            "etc/assets/", "etc/fps/", "etc/editor/")
SKIP_EXT = (".py", ".pyc")

INCLUDE_RE = re.compile(r"^\s*include\s+([\w./-]+)", re.M)
PATH_RE = re.compile(r"\"(etc/[^\"]+)\"")


def fail(msg):
    print("publish: " + msg, file=sys.stderr)
    sys.exit(1)


def rel(path):
    return os.path.relpath(os.path.abspath(path), ROOT).replace(os.sep, "/")


def resolve_scene(arg):
    path = os.path.abspath(arg)
    if path.endswith(".flecs"):
        scene = path
    else:
        scene = path.rstrip("/") + ".flecs"
    if not os.path.isfile(scene):
        fail("no scene file '%s'" % rel(scene))
    return rel(scene)


def add_scene(files, scene):
    files.add(scene)
    folder = os.path.join(ROOT, scene[:-len(".flecs")])
    if os.path.isdir(folder):
        for dirpath, dirnames, filenames in os.walk(folder):
            dirnames[:] = [d for d in dirnames if not d.startswith((".", "__"))]
            for f in filenames:
                if f.startswith(".") or f.endswith(SKIP_EXT):
                    continue
                files.add(rel(os.path.join(dirpath, f)))


def collect(scene):
    files = set()
    add_scene(files, scene)
    missing = set()
    scanned = set()
    while True:
        pending = [f for f in files if f.endswith(".flecs") and f not in scanned]
        if not pending:
            break
        for f in pending:
            scanned.add(f)
            with open(os.path.join(ROOT, f), encoding="utf-8", errors="replace") as fp:
                text = fp.read()
            base = os.path.dirname(f)
            for name in INCLUDE_RE.findall(text):
                target = os.path.normpath(os.path.join(base, name + ".flecs")).replace(os.sep, "/")
                if target in files:
                    continue
                if os.path.isfile(os.path.join(ROOT, target)):
                    if os.path.dirname(target) == "etc/scenes":
                        add_scene(files, target)
                    else:
                        files.add(target)
                else:
                    missing.add("%s (include in %s)" % (target, f))
            for path in PATH_RE.findall(text):
                if path.startswith(EMBEDDED) or path in files:
                    continue
                if os.path.isfile(os.path.join(ROOT, path)):
                    files.add(path)
                elif os.path.isdir(os.path.join(ROOT, path)):
                    continue
                else:
                    missing.add("%s (in %s)" % (path, f))
    return sorted(files), sorted(missing)


def scene_title(scene, name):
    with open(os.path.join(ROOT, scene), encoding="utf-8", errors="replace") as fp:
        first = fp.readline().strip()
    m = re.match(r"//\s*([^:]+):\s*(.*)", first)
    if m:
        return m.group(1).strip(), m.group(2).strip()
    return name.replace("_", " ").title(), ""


def build_engine():
    cmd = ["bake3", ENGINE_APP, "--target", "em", "--cfg", "release", "--local-env"]
    print("$ " + " ".join(cmd))
    if subprocess.call(cmd, cwd=ROOT) != 0:
        fail("engine build failed")


def engine_artefacts():
    out = os.path.join(ROOT, ENGINE_APP, "bin", "Em-release")
    js = os.path.join(out, ENGINE_NAME + ".js")
    wasm = os.path.join(out, ENGINE_NAME + ".wasm")
    if not os.path.isfile(js) or not os.path.isfile(wasm):
        fail("no engine build in %s, run without --no-build" % rel(out))
    return js, wasm


def digest(*paths):
    h = hashlib.sha256()
    for p in paths:
        with open(p, "rb") as fp:
            h.update(fp.read())
    return h.hexdigest()[:12]


def template(name):
    with open(os.path.join(TOOL_DIR, name), encoding="utf-8") as fp:
        return fp.read()


def publish_engine(site):
    js, wasm = engine_artefacts()
    version = digest(js, wasm)
    dst = os.path.join(site, "games", ENGINE_DIR)
    os.makedirs(dst, exist_ok=True)
    shutil.copy2(js, os.path.join(dst, ENGINE_NAME + ".js"))
    shutil.copy2(wasm, os.path.join(dst, ENGINE_NAME + ".wasm"))
    loader = template("loader.js")
    loader = loader.replace("{{VERSION}}", version)
    loader = loader.replace("{{ENGINE}}", ENGINE_NAME)
    loader = loader.replace("{{WASM_SIZE}}", str(os.path.getsize(wasm)))
    with open(os.path.join(dst, "loader.js"), "w", encoding="utf-8") as fp:
        fp.write(loader)
    print("engine  games/%s/%s.wasm (%.1f MB, version %s)" % (
        ENGINE_DIR, ENGINE_NAME, os.path.getsize(wasm) / 1e6, version))


def publish_game(site, scene, name, title, description, files):
    dst = os.path.join(site, "games", name)
    if name == ENGINE_DIR:
        fail("'%s' is reserved for the engine" % name)
    if os.path.isdir(os.path.join(dst, "etc")):
        shutil.rmtree(os.path.join(dst, "etc"))
    os.makedirs(dst, exist_ok=True)
    size = 0
    entries = []
    for f in files:
        target = os.path.join(dst, f)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copy2(os.path.join(ROOT, f), target)
        n = os.path.getsize(target)
        size += n
        entries.append({"path": f, "size": n})
    game = {"scene": scene, "files": entries}
    page = template("index.html")
    page = page.replace("{{TITLE}}", html.escape(title))
    page = page.replace("{{DESCRIPTION}}", html.escape(description or title))
    page = page.replace("{{ENGINE_DIR}}", ENGINE_DIR)
    page = page.replace("{{GAME}}", json.dumps(game, separators=(",", ":")))
    with open(os.path.join(dst, "index.html"), "w", encoding="utf-8") as fp:
        fp.write(page)
    print("game    games/%s (%d files, %.1f MB)" % (name, len(files), size / 1e6))


def serve(site, port, name):
    print("serving http://localhost:%d/games/%s/ (ctrl+c to stop)" % (port, name))
    try:
        subprocess.call([sys.executable, "-m", "http.server", str(port)], cwd=site)
    except KeyboardInterrupt:
        pass


def main():
    p = argparse.ArgumentParser(prog="tools/publish",
        description="Publish a box scene as a web game to the flecs.dev site")
    p.add_argument("scene", help="scene to publish, e.g. etc/scenes/castle")
    p.add_argument("--name", help="folder under games/ (default: scene name)")
    p.add_argument("--title", help="page title (default: from the first comment of the scene)")
    p.add_argument("--site", default=DEFAULT_SITE, help="site repository (default: %(default)s)")
    p.add_argument("--no-build", action="store_true", help="reuse the last engine build")
    p.add_argument("--no-engine", action="store_true", help="only update the game files")
    p.add_argument("--serve", type=int, nargs="?", const=8000, metavar="PORT",
        help="serve the site locally after publishing")
    args = p.parse_args()

    site = os.path.abspath(args.site)
    if not os.path.isdir(os.path.join(site, "games")):
        fail("no games folder in site '%s'" % site)

    scene = resolve_scene(args.scene)
    name = args.name or os.path.basename(scene)[:-len(".flecs")]
    title, description = scene_title(scene, name)
    if args.title:
        title = args.title

    files, missing = collect(scene)
    for m in missing:
        print("warning: missing file %s" % m, file=sys.stderr)

    if not args.no_engine:
        if not args.no_build:
            build_engine()
        publish_engine(site)
    elif not os.path.isfile(os.path.join(site, "games", ENGINE_DIR, "loader.js")):
        fail("the site has no engine yet, run without --no-engine")

    publish_game(site, scene, name, title, description, files)
    print("url     https://www.flecs.dev/games/%s/" % name)

    if args.serve:
        serve(site, args.serve, name)


if __name__ == "__main__":
    main()
