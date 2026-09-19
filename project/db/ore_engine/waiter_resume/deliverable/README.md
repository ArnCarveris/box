# Several waiters resume on one action in a scene

## Built

The missed raise `ActionWaiters` keeps for a phase that reached no waiter was
single shot: the first task that caught up removed it, so the other templates of
a scene, whose async blocks start in the same tick, awaited forever. In
`src/input/events.c` a missed raise now resolves every waiter that catches up and
is dropped at the next raise pass instead, and a `resolving` guard keeps the await
a resumed task registers again from taking the same raise twice.

## Verified

`test/script` gained an `Async` suite with two cases (a manual template instance
resumes, several do on one action) and `test/integration/command/waiter_resume`
drives two waiters plus an agent in a loaded scene; both fail before the fix and
pass after. `test/script` (23), `test/input` (98) and all 17 command scenes pass.
Lint 0. `bench/input` unchanged.

## Left out

The root cause is box side, so flecs was not touched. `ecs_script_template_update`
still re-instantiates a manual template with every input, which restarts its async
blocks on any mut change; passing the recorded `changed` mask instead is a flecs
follow up.
