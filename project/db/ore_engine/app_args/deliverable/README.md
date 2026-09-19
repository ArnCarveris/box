# Application command line options

## Built

An application adds options of its own as `AppOption` entities before it calls
`box_parse_args`, so `--stresstest` is a component the scene reads and not an argument the
application passes around. The name of the entity is the flag, `AppOption.member` is the
member of a singleton component the value lands in, written through reflection so an int, a
float, a bool, a string or an enum constant all parse, `fallback` is what the bare flag takes
and `description` is what `--help` prints. The value comes from `--flag=value`, from the
fallback, from `true` when the member is a bool, or from the next argument. `--help` prints
the engine options and the options of the world, which `box_args_usage` also returns as text,
and a flag nobody claims is still the same error with the same usage message.

## Verified

9 new tests in `core.Args`, 43 in `test/core`, all green; lint 0. All 16 command scenes pass
unchanged and `apps/box_app` is untouched.

## Left out

The member must be a direct member of a singleton component; an option that runs a callback
or that collects the arguments it does not know was not needed by either form Ore Else uses.
No bench: the parse runs once before the first frame.
