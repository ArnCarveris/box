# Linear script evaluation in flecs

Script evaluation is quadratic in the statement count through flecs_script_dep_entity_may_match, paid once at scene load; make it linear in the flecs repository.

## Details

Profiling the checkpoint edit path showed the remaining cost at scene load in the dependency matching of the script evaluator, which compares statements pairwise. Index the dependency candidates by the entity or path they name so each statement resolves against a small set, keep the flecs `test/script` suites green, add a flecs test that evaluates 10k statements with a time bound, commit in the flecs repository with a single line message and never push. Then record the `templates_100k` bench scene load and the `bench/script` load cases here.

Verification: the flecs test, `bench/script` before and after in the deliverable, `test/script` unchanged.

## References

- `project/db/checkpoints/edit_perf/deliverable/README.md`, `../../flecs/src/addons/script`
