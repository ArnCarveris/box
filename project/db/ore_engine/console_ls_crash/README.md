# Crash on ls in the console

Opening the console with the backtick key and issuing ls crashes the application; the console must run every shell command the HTTP terminal runs.

## Details

Reported from a manual run of `box_app`: press backtick, type `ls`, press return, the process
dies. The HTTP terminal runs `ls` in `src/command/shell.c` without trouble, so the crash sits in
the path the console adds: the `TerminalOutput` the session writes back, the `ConsoleLog` that
appends it to the scrollback, the text layout of a scrollback line, or the history and
completion the `TextInput` runs on return. Reproduce it first with a command scene under
`test/integration/command` that opens the console, sends `ls` through the console input as a
key sequence and captures, then with a debug build under the debugger to get the trace, and
narrow it to a `test/ui` or `test/command` unit case that fails before the fix. Fix it in `src`
and keep both cases.

Verification: the unit case and the command scene fail before the fix and pass after,
`test/ui`, `test/command` and every command scene green, and `ls` runs in the console of
`box_app` by hand.

## References

- `src/command/shell.c`, `src/ui/console.c`, `src/ui/text.c`, `etc/ui/console.flecs`,
  `test/integration/command/console.cmd`, `project/db/ui/console/README.md`
