# Wasm request export

Built with `bake3 build box_app --target em --local-env=wasmreq --cfg release`, served
with `bake3 run box_app --target em --local-env=wasmreq --cfg release`, checked in
Chrome 153 (headless, SwiftShader) over CDP on `http://localhost:8080/box_app.html`:

- `window.boxRequest` exists once the runtime is initialized
- `boxRequest('GET', '/terminal/new', '')` -> `200\ntext/plain\ns1`
- `boxRequest('GET', '/terminal/exec?session=s1&cmd=help', '')` -> `200\ntext/plain\n`
  followed by the command list, starting with `add <path:string> ...`
- `boxRequest('GET', '/nope', '')` -> `404\ntext/plain\nunknown endpoint`

Sessions are named from `s1`, not `s0`. `box_app.js` maps `Module["_box_request"]` to a
wasm export, so `-sEXPORTED_FUNCTIONS` keeps it through linking. On web the command
server object is created but never started, so requests only reach it through
`box_request`.

Manual repeat: run the two commands above and type the calls in the page console.
