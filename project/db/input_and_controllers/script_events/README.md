# Action script events

Actions raised as script events so a script can await a press inside an async block.

## Details

Actions raise events through the flecs script event framework so await on.pressed(Forward) and the down and up variants resume a script in the frame the action fires. Events carry the action entity and the source so a handler can tell a key press from a UI button or a command. This reuses the event module that picking uses, so one mechanism covers input and interaction.

Verification: unit tests run a script that awaits an action, inject the key and check the script resumed in the same frame, and cover several waiters, unsubscribing and an action emitted without a key. A command based integration test drives a script through the action commands.
