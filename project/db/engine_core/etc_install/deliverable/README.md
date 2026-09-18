# bake3 installs etc into the environment

Implemented in the bake3 repository (six commits on its main branch). Every build of a
public project mirrors `<project>/etc` into `$BAKE_HOME/etc/<project id>` (`~/bake3`
globally, `.bake/local_env[/<name>]` for local environments), refreshing changed files,
deleting stale ones and skipping private projects; cleanup and reset remove it. Processes
started by `bake3 run`, `test` and `bench` receive `BAKE_HOME` and `BAKE_ENVIRONMENT`, and
`bake3 info` prints the etc path. Seven bake3 self tests cover install, refresh and
removal, the private skip, both local environment paths, the exported variables in all
three environments and the info line; the full bake3 suite (137 tests) passes and the
installed binary was rebuilt. `tests.json` is the bake3 run for this feature is kept in
the bake3 repository; this task has no box unit tests.
