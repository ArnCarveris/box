# Stats command with renderer and GPU panels

A stats command that opens statistics panels defined as script templates, with a renderer panel of per frame counts and memory and a GPU panel of pass timings.

## Details

`src/renderer/instrument.h` routes every draw, dispatch, pipeline and bind group set, pass begin, queue write and resource creation of the renderer through counting wrappers; a thread local pointer is only set while the world asks for counts, so a closed panel costs one load per call. Resource memory is tracked at creation and release per device. A counted frame is published under a lock only once it is whole, after the read back of the arguments of its indirect draws, so `RendererStats` never shows half a frame. `box.monitor` loads the templates of `etc/ui/stats_panels.flecs` and the layout of `etc/ui/stats.flecs`, whose exported variables add the singleton their panel reads; the `stats` command (`src/command/stats.c`) switches the variables and prints the numbers.

Verification: `test/render` Counters checks the counts against the scene, the device report of wgpu-native and the instance count of the culling read back; `test/monitor` StatsPanels and `test/command` Stats cover the panels and the command; `stats_overlay` baselines the panel layout and `bench/render` Counters measures the cost with counting off, on and with every detail.
