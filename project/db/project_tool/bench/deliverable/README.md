# Native benchmark projects

Four bake3 bench projects under `bench/`, `tools/bench.py` to run them all, the
benchmark process rules in `README.md` and `CLAUDE.md`, and `pm.py` reading the
bake3 report format. Medians from a release run with `--local-env=bench`:

| Project | Case | Median | Items/s |
|---|---|---:|---:|
| command | Dispatch.help | 7.92 us | 0.1 M |
| command | Dispatch.ls_scope | 413.61 us | 2.4 M |
| command | Dispatch.path_lookup | 1.32 us | 0.8 M |
| command | Dispatch.set_expression | 3.41 us | 0.3 M |
| geometry | Generate.sphere | 8.96 us | 242.7 M |
| geometry | Generate.box | 169.0 ns | 141.9 M |
| geometry | Generate.torus | 6.81 us | 159.5 M |
| geometry | Generate.capsule | 9.19 us | 244.7 M |
| geometry | Cache.hit | 25.1 ns | 40.0 M |
| render | Extract.static_1_changed | 339.5 ns | 2.9 M |
| render | Extract.static_100_changed | 6.01 us | 16.9 M |
| render | Extract.static_1000_changed | 54.07 us | 18.3 M |
| render | Extract.dynamic_10k | 190.77 us | 51.9 M |
| transform | Static.set_leaf_position | 193.9 ns | 5.1 M |
| transform | Static.set_root_position | 450.27 us | 22.3 M |
| transform | Static.reparent | 3.78 us |  |
| transform | Dynamic.propagate_subtree | 18.47 us | 54.1 M |

`tests.json` is `python3 project/test_pm.py --json`: 12 cases, all passing.
`<project>.json` are the bake3 reports with samples and confidence intervals.
