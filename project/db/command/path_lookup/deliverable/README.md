# Entity paths through flecs lookup

Paths now go through `ecs_lookup_path_w_sep` relative to the session cwd (absolute
with a leading dot), so `#123` raw ids and `\.` escaped dots resolve; the custom
walker remains only for `..`, empty elements and `touch` creation. `ls` escapes
dots in names so its output pastes back into a command. Verified with four new
Shell cases (49 in the suite) and the five command scenes, in a worktree at HEAD
because the main tree was mid edit by the effects task; `tests.json` is that run.
