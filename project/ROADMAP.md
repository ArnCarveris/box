# Roadmap

Every epic carries a `priority` in its `progress.json`: an integer where lower
runs first, 100 when the field is missing, seeded in steps of ten from the order
this file used to list so there is room to slot an epic in between. Set one with
`pm.py new epic <name> "Title" --priority 65`, or from the priority box in the
epic panel of the status page, which posts to `/api/priority`. The tool derives
the execution order from the priorities and the dependencies: a task is ready
when its dependencies are complete, and the ready task from the epic with the
lowest priority comes first, then the oldest. Dependencies say what actually
blocks what, so `pm.py check` warns when an epic sits before an epic it depends
on, and reports tasks that can never become ready because of a cycle.

Adjustments made so the order is consistent with the dependency graph:

- Lighting runs before views: clustered lights and IBL work against the single
  main view; the layers task of views integrates lights with layers afterwards
  and depends on clustered_forward.
- Light baking ships synchronous first; the job_system task of responsiveness
  moves it onto jobs and depends on light_baking.
- The view task depends on material_layers so its per view toggles exist.
- Rain moved from weather to a new particles/rain task; weather/states ships
  overcast and fog.
- controller_collision moved from controllers to collision, since it needs the
  spatial index.
- The frame spike harness of responsiveness measures frames itself instead of
  depending on the system monitor.
