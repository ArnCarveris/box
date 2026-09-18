# Box project notes

Read `README.md` for build and run commands and `docs/ARCHITECTURE.md` for the
module layout. The design brief is `design/DESIGN.md`.

## Style

`design/coding_style.c` is the reference. `tools/lint.py` enforces it:

```sh
python3 tools/lint.py          # report violations in include, src, apps and test
python3 tools/lint.py --fix    # reformat signatures and blank lines, then report
```

Run the linter before every commit; it must report 0 problems. Rules it checks:
file header with `@file` and `@brief`, a doc comment with `@param` and `@return`
on every static function and header prototype, one parameter per line and the
opening brace on its own line, no comments inside function bodies (tests may
start with one `// epic/task` reference line), a blank line before `return` and
after every block, no `return` inside a nested block (use `goto done`), lines
under 100 characters, functions under 200 lines, files under 2000 lines, and a
doc comment on every type and member.

## Build environments

Use a private environment when working next to other agents:

```sh
bake3 --local-env=<name>
bake3 run test/core --local-env=<name> -- -j 8
```

## Tracking work

Work is tracked with `project/pm.py` (see README). A task is complete only when
its `deliverable/tests.json` exists, produced with `--json` from the affected
test suites.
