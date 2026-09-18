# App singleton and frame loop

Command line parsing, process folder, stop conditions, CPU budget and oversubscription check.

## Details

`box.core` registers the reflected `App` singleton with headless, frames, timeout, cpu_budget, target_fps, frame and process_dir members. `box_parse_args` fills it from `--headless`, `--frames`, `--timeout`, `--cpu-budget`, `--fps` and scene file paths, and rejects headless runs without a stop condition or with an unloadable scene.

`box_run` progresses the world until the frame or timeout limit is hit or the world quits. It refuses to start headless when the one minute load average exceeds the core count, and when a budget below one is set it sleeps so the process CPU time stays within the budget. Every process creates `/tmp/box/<pid>` and removes it when the world is destroyed. On wasm the loop is driven by `emscripten_set_main_loop`.
