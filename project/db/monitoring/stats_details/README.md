# Pass, pipeline, bind group, buffer and compute panels

Detail panels of the stats command that break the renderer counts down per pass, pipeline, bind group slot, buffer usage and compute pass.

## Details

The renderer fills per pass, per pipeline (named by its label), per pass and bind group slot and per buffer usage tables when the matching `BOX_COUNT_` flag is asked for; `PassStats`, `PipelineStats`, `BindGroupStats`, `BufferStats` and `ComputeStats` ask for their flag while they exist and are filled from the same whole frame as `RendererStats`. The `pass`, `pipeline`, `bindgroup`, `buffer` and `compute` variables of `etc/ui/stats.flecs` show the table templates of `etc/ui/stats_panels.flecs`, which stack under the other panels, and `stats <panel>` prints the tables.

Verification: `test/render` Counters checks that every table adds up to the frame totals and stays empty until asked; `test/monitor` StatsPanels and `test/command` Stats cover the panels and their output.
