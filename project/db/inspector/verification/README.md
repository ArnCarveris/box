# Inspector verification

Browser driven checks of the page against a native headless instance and against the command epic wasm image, with screenshots as deliverables.

## Details

Start a native instance headless with a surface and `--timeout infinite`, open the inspector from the project tool with the HTTP link, run `help`, `ls` and `capture` from the console and confirm the render pane updates; then open the epic link for the command epic wasm image and repeat. Screenshots of both are the deliverable, together with a short note listing what worked and any browser or CORS issues found.

Where a browser can be driven from the agent environment, the run is scripted; otherwise the steps are documented in `apps/box_inspector/README.md` for a manual run. Bugs found in the transports or endpoints are fixed in their tasks' source files with unit tests.

## References

- every other task in the `inspector` epic
