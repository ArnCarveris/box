# Box inspector

`index.html` is one static page without a build step: a command console and a render pane. It
talks to a running instance over the HTTP command API, or to a wasm image loaded in the page
through its `box_request` export, and both transports present the same `request(method, path,
body)` interface, so the page code never knows which one it uses. The console opens a terminal
session on connect, runs `help` and sends every line to `/terminal/exec`; file names in a reply
are fetched from `/resource` and shown as thumbnails. The render pane asks `/capture` on a timer
(1 Hz, pausable, 0.5 - 5 s) and shows the PNG with the frame number; a wasm image also renders
live in the canvas, toggled with **Show capture** / **Show live**.

## Console output

`colorize(text, command)` in `inspector-core` turns a result into HTML with one span per
token, never one per character, so a long log stays cheap. The echoed `$ line` shows the
command name in bold accent; entity paths get one colour and component or type names
another, with the namespace before the last dot kept quiet; `<name:type>` in `help`, member
names, numbers and strings inside `{...}` each get their own token; tab separated output
(`next`, `grep`, `logs`) becomes an aligned grid; `logs` levels are coloured by severity.
An error (status 400) stays plain text in the error colour, and every result is followed by
one blank line so consecutive commands are easy to tell apart. The colours are `--syn-path`,
`--syn-type`, `--syn-num` and `--syn-str` on `:root`, with a dark mode variant of each.

## Query parameters

- `http=<url>`: HTTP mode on that endpoint, connects on load
- `wasm=<url>`: wasm mode on that `box_app.js`, connects on load
- `scene=<args>`: engine arguments for the wasm image, such as a scene path

Without parameters the page opens disconnected on `http://localhost:27750` and remembers the last
endpoint, image, camera, interval and history in `localStorage`. `project/pm.py serve` serves it at
`/inspector`, linked from the header and from every epic with a `deliverable/wasm/` image.

## Running it

Against a native instance, where the `Surface` gives the headless run a render target:

```sh
printf 'using box.surface\n\n$ { Surface: {width: 640, height: 360} }\n' > /tmp/surface.flecs
bake3 run box_app --local-env --cfg release -- --headless --timeout infinite \
    --fps 30 --port 27750 /tmp/surface.flecs etc/scenes/primitives.flecs
python3 project/pm.py serve   # /inspector?http=http://localhost:27750
```

Against a wasm image, copied into an epic deliverable and served by the same tool:

```sh
bake3 build box_app --target em --local-env --cfg release
cp .bake/local_env/<env>/wasm32-Emscripten/release/bin/box_app/box_app.{html,js,wasm} \
    project/db/<epic>/deliverable/wasm/
# /inspector?wasm=/deliverable/<epic>/deliverable/wasm/box_app.js&scene=etc/scenes/primitives.flecs
```

## Verification

`node test.js [--json out.json]` runs the `inspector-core` script out of the HTML against stubbed
transports. In the browser: connect, run `help`, `pwd`, `ls`, `cd box`, `cd /` and `cat camera`;
run `set camera Position "{2, 6, -14}"` and watch the render pane show the new view within a
second; run `capture` and open the thumbnail, which links to a `/resource/<file>?session=<id>`
PNG; run `render sphere` and open the thumbnail it prints once that frame rendered; run
`clear` to empty the log without a request; press **Disconnect**. Run `grep Position` and
`logs` and check the columns line up, and toggle the theme to read the log in both.
`project/db/inspector/page/deliverable/console_colors.png` is a headless Chrome shot of the
log after `help`, `ls`, `cat camera` and an unknown command.

## Known limitations

- Cross origin requests must stay simple: the POST used for command lines over 1500 characters
  sends `Content-Type: text/plain`, never JSON, so the browser does not preflight it. The instance
  answers with `Access-Control-Allow-Origin: *`.
- Images built before the `box_request` export only render, and the console says so; an image that
  fails to start shows the emscripten `Aborted()` message instead.
- A wasm image needs WebGPU: Chrome on a real GPU works, `--headless=new` on macOS has no
  `navigator.gpu` and the image aborts.
- `/capture` answers 202 while a capture is in flight, so the first refresh is "capture pending".
