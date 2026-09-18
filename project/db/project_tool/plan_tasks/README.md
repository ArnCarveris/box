# Task level plan with epic priorities

Replace the explicit epic order with epic priorities, order tasks by dependencies first and priority second, and show tasks on the plan page laid out by dependency.

## Details

The explicit epic order in `plan.json` goes away. Each epic gets a `priority` field in its `progress.json` (lower runs first, default 100), editable from the epic panel on the status page through a small endpoint, and the recommended execution order is derived: a task is ready when its dependencies are complete, and among ready tasks the one from the epic with the lowest priority comes first, then creation time. The tool exposes that order as the "next up" list on the board and uses it to sort unstarted epics and tasks everywhere the page lists them; `pm.py check` warns when an epic's priority puts it before an epic it depends on.

The plan page shows tasks instead of epics: one node per task colored by status and grouped by epic color, columns by dependency depth left to right, dependency edges drawn between tasks and cross epic edges emphasized, with the epics as swimlane labels or a legend and a filter to show one epic or only unfinished tasks. Nodes are not draggable; hovering shows the task's dependencies and dependents. Pan, zoom and the filter persist in the UI state like the rest of the page.

Verification: tracker tests for the priority field, the derived order with a dependency tie, the endpoint validation and the check warning; a node stub DOM check of the task layout (columns by depth, no overlaps) and a headless Chrome pass without console errors; the README and roadmap updated to describe priorities instead of the order file.

## References

- `project/pm.py`, `project/index.html`, `project/test_pm.py`, `project/ROADMAP.md`
