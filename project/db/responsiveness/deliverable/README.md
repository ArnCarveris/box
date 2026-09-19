# Responsiveness: completion report

## What was built
- `box.job`: a worker pool sized from the core count with a heap keyed by priority then distance to the camera eye, main thread completions drained in PreUpdate under a millisecond budget, cancellation and superseding by group, clean shutdown with jobs in flight, and a main thread fallback on the web where there are no threads. Clients: SVG rasterization now runs as banded jobs prioritized by the nearest material that uses the texture, with a placeholder until the upload; light baking keeps its own per frame probe budget.
- `box.script`: templates become manual on registration through the scheduled template update support of flecs, instances arrive pending and a PreUpdate system expands them under a count and time budget nearest to the camera first, reordering with a heap when the camera moves, while non template content appears immediately; `enabled: false` expands everything at once and the integration harness uses it so captures stay deterministic.
- The frame spike harness: the bench app reports p50, p99, max and the worst eight frames with the slowest system of each through the system monitor, a spike budget as a multiple of the mean fails a run, the runners honour per scene argument files, and a combined stress scene loads templates, rebakes probes, rasterizes SVGs and rebuilds cascades at once.
- The Parent hierarchy support originally planned here moved to the scene core epic and is complete.

## Verification
- 938 unit tests across twelve projects, all passing; `test/job` 20, `test/script` 18, the `SvgJobs` suite in `test/render` and the `Spikes` suite in `test/monitor`.
- Stress scenes with the spike budget: `static_100k` 2.19 ms mean, 4.43 ms p99, 4.55 ms max, no spike, `material_100k` 2.12 ms mean, 4.70 ms p99, 5.38 ms max, no spike, `templates_100k` 1.11 ms mean, 1.82 ms p99, 2.38 ms max, no spike, `spikes` 7.39 ms mean, 15.13 ms p99, 21.09 ms max, no spike; the templates scene loads 100k instances at a flat 1.1 ms with no spike and completes over about four thousand frames at the default budget.
- Microbenchmarks in `bench/` against the effects epic: `SvgJobs.rasterize_1000_while_rendering` 58% slower because the texture pager now lets all thousand textures land instead of stopping at the old layer cap, `Bake.invalidate_one_of_100k` 11% slower at the noise floor, `Job.submit_100k` 27% faster; 100k jobs submit at 944k per second and a frame draining 10k completions stays inside its 2 ms budget.
- Build reports and the wasm image in the deliverable, open it through the status page Inspect link of this epic.

## Lessons
- Decoupling the job eye from the render module through a per frame setter kept `box.job` free of render dependencies.
- flecs' scheduled template updates give incremental loading without a second script pass; the order refresh on camera movement is the only cost.
- A spike budget relative to the mean plus per frame system attribution turns a p99 number into a named offender.

## Follow up
- `responsiveness/script_eval_perf` (closed after the checkpoints epic): the script evaluator compared each component statement with every earlier one to find its owner, so a scene load was quadratic in the statement count. Flecs commit `e1b0775d9` indexes owners by a hash of the entity path they name; a 10k statement scene loads in 42 ms instead of 755 ms and a 50k one in 216 ms instead of 18.8 s, recorded in the `bench/script` `Load` suite.
- `responsiveness/acquire_stall` (closed after the LOD epic): the one 4.8 ms frame in the 50k root bench scenes was the probe bake sorting its entire dirty set by camera distance on the frame incremental template loading finished, 49k jobs for one frame. The take now orders only as far as the probe budget reaches, with a quickselect for the nearest run; both scenes are back at the standard spike budget with maxima of 2.2 and 2.4 ms.

## Not in this epic
- The combined stress scene's tail frames attribute to the surface acquire, the wait on the GPU, at 15% headroom under the budget; a triple buffered acquire or an earlier submit is the follow up.
- Light baking still uses its own per frame budget rather than the job pool.
