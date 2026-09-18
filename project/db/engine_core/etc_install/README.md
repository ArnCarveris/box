# bake3 installs etc into the environment

bake3 installs the etc folder of a public project to <BAKE_HOME>/etc/<project> on build, for global and local environments, and exports BAKE_HOME to processes it runs.

## Details

bake v2 installed a project's `etc` next to its binaries so dependents could find engine assets; bake3 only embeds `etc` on wasm. On every build of a project with `"public": true` bake3 copies its `etc` folder to `<BAKE_HOME>/etc/<project id>` (the environment root, `~/bake` globally and `.bake/local_env/<name>` for local environments), replacing stale files and removing files that disappeared from the source, and skips private projects. Processes started with `bake3 run` receive `BAKE_HOME` in their environment (and `BAKE_ENVIRONMENT` naming the local environment) so a framework can locate its installed assets without knowing how it was built; `bake3 info` prints the etc install path.

Verification: bake3 self tests for the copy on build, the refresh and removal of stale files, the private project skip, the local environment path and the exported variables; the README of bake3 documents the layout. The installed binary is rebuilt and reinstalled.

## References

- bake3 `README.md`, section on `etc` and local environments
- bake v2 `BAKE_HOME`, `BAKE_ENVIRONMENT` conventions
