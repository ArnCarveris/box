# Action script events

`box.input` registers `pressed`, `down` and `up` as async script functions under the existing
`flecs.script.on` scope, so a task awaits an action the way it awaits a click:

    async { const src = await on.pressed(actions.Forward) }

They take the action entity and return the source that fired it: the binding entity for a key, or
the entity `box_input_emit` was given for a UI button or a command. `on.pressed` resolves on the
frame the action becomes active, `on.down` while it is active (the same frame for a fresh press)
and `on.up` on the frame it stops, the `ActionPressed`, `ActionHeld` and `ActionReleased` phases of
the per frame event list. `BoxInputRaise` runs in OnLoad right after `BoxInputActions` and resolves
the future of every waiter the events of the frame match, before `flecs.script.ProgressTasks`
resumes the tasks in PreUpdate, so a script resumes in the frame its key fires; `box_input_emit`
resolves the waiters of its action immediately. The private `ActionWaiters` singleton holds one
entry per pending await and its cancel callback drops it when a task is cancelled, which is what
deleting the entity of the task does.

63 tests pass in `test/input`, 7 of them in `Events`; `Events.resume_100_waiters`, one press that
resumes a hundred awaiting tasks, costs 81 us.
