# Roadmap

Epic order after the command and inspector epics, as general guidance (the
`priority` field in each epic's `progress.json` orders unstarted epics on the
status page; dependencies say what actually blocks what):

1. lighting
2. textures_and_materials
3. views_and_culling
4. effects
5. sky
6. responsiveness
7. geometry_extensions (custom meshes, 2D shapes, splines)
8. input_and_controllers
9. interaction
10. ui
11. transitions
12. instrumentation (monitoring and checkpoints)
13. lod
14. particles

Adjustments made so the order is consistent with the dependency graph:

- Lighting runs before views: clustered lights and IBL work against the single
  main view; the layers task of views_and_culling integrates lights with layers
  afterwards and depends on clustered_forward.
- Light baking ships synchronous first; the job_system task of responsiveness
  moves it onto jobs and depends on light_baking.
- The view task depends on material_layers so its per view toggles exist.
- Rain moved from sky/weather to a new particles/rain task; sky/weather ships
  overcast and fog.
- controller_collision moved from input_and_controllers to interaction, since
  it needs the spatial index.
- The frame spike harness of responsiveness measures frames itself instead of
  depending on the system monitor.
