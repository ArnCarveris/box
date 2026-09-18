# Roadmap

Epic order after the command and inspector epics, as general guidance. The
order the tool uses lives in `project/db/plan.json`, which seeded it from this
list; it sorts unstarted epics on the status page and is edited by dragging the
order arrows on the Plan tab (dependencies say what actually blocks what):

1. lighting
2. textures
3. materials
4. views
5. culling
6. effects
7. sky
8. weather
9. responsiveness
10. meshes (custom meshes, shape registry, glTF)
11. shapes_2d
12. splines
13. input
14. controllers
15. collision
16. picking
17. ui
18. transitions
19. monitoring
20. checkpoints
21. lod
22. particles

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
