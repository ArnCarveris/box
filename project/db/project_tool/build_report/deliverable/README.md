# bake3 build timing report

Implemented in the bake3 repository (five commits on its main branch, binary reinstalled).
`--build-json <file>` on build, rebuild, run, test and bench writes tool and host info, cfg,
target (`em` or the native triplet), environment, the git sha, dirty flag and branch, the
wall clock total, `ok`, totals per step kind and per project, and a step tree covering
discovery, bundle fetch and build, generated files, every compiled file with its object
path and real parallel wall clock, link with an embed child on the web target, and the etc
install; failures keep the report with the failing step marked. Six bake3 self tests cover
the native report, the failure case, git inside and outside a repository, run and test
acceptance, option rejection and the wasm target; the full bake3 suite passes apart from a
pre-existing benchmark flake. `tests.json` mirrors those six cases.
