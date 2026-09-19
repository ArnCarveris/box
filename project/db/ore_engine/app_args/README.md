# Application command line options

Let an application add its own command line options to box_parse_args instead of having it reject every flag it does not know, which is what the game's --stresstest needs.

## Details

`box_parse_args` knows `--headless`, `--frames`, `--timeout`, `--cpu-budget`, `--fps`, `--port` and a
scene file, and fails with a usage message on anything else that starts with a dash. Ore Else is run
as `ore_else --stresstest` and `--stresstest=45`, so `ore_app/skeleton` and `ore_release/stresstest`
cannot parse their flag without either forking the parser or stripping arguments before the call.
Give the application a way in: a table of options it registers before the parse (name, whether it
takes a value, and where the value lands, a component member or a callback), so its flags print in
the usage text with the engine's, or, if that is heavier than it needs to be, have the parse collect
the arguments it does not know and hand them back for the application to read. Either way the
unknown option error stays for a flag nobody claimed, and `--help` lists both sets.

Verification: Unit tests in test/core that a registered option parses with and without a value, that an
unclaimed flag still fails, that the usage text lists it and that the scene argument still loads;
apps/box_app unchanged.

## References

- project/db/engine_core/deliverable/README.md
- project/db/ore_release/stresstest/README.md
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/src/main.c
