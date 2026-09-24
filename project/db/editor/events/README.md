# Unhandled pick events and exclusive text focus

Events no handler took reach waiters on box.picking.Unhandled, a strict view picks its own layers only and focusing one TextInput blurs the others.

## Details

box_pick_raise delivers an event that nothing in the parent chain or behind a passthrough tree took to the waiters on the Unhandled tag, including presses and clicks on empty space, which a script hears with `await on.click(Unhandled)`. The picking update became an immediate system, so a hover that re-evaluates a template restarts its tasks before the press of the same frame is raised, and a press that found no waiter is kept while the button is held for the first waiter that registers on it.

box_collision_ray_filter takes a filter, and a pick through a view with strict layers only considers the members of those layers, while a pick through any other view skips the members of the UI layers, so a gizmo drawn over the scene is picked before the geometry it covers. Setting focused on a TextInput, or calling text.focus(element), takes the focus from every other input; text.focus descends to the first input below the element.

Verified with the new cases of the Events and Pick suites of test/picking and the TextInput suite of test/ui.
