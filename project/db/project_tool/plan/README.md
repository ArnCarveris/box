# Plan page

A node diagram of the epics with dependency edges and draggable execution order arrows stored in project/db/plan.json.

## Details

A "Plan" tab shows every epic as a node colored by status, laid out left to right by dependency depth with an automatic layout that avoids overlaps, and the dependencies from `progress.json` drawn as grey edges. On top of that an independent chain of white arrows shows the execution order: one arrow from each epic to the one planned after it. The order lives in `project/db/plan.json` as an ordered list of epic names; the `priority` field currently in each epic's `progress.json` is removed and the tool sorts unstarted epics by their position in `plan.json` instead, with epics missing from the file appended in creation order.

Dragging the head of an order arrow onto another node re-links the chain: the dragged epic moves to just after the target and the chain is repaired so it stays a single sequence. Arrows of epics that are in progress or complete cannot be dragged and their nodes are not valid drop targets for reordering before them; the page shows the reason on hover. Changes are saved with a `POST /api/plan` endpoint that validates the list (every name an existing epic, no duplicates, finished and in progress epics kept in their start order at the front) and rewrites `plan.json`. A dependency violation in the order (an epic planned before one it depends on) is highlighted in red but still allowed.

Verification: tracker tests for reading and writing `plan.json`, the validation rules and the sort order, plus a node stub DOM check of the layout and the drag handling; the page keeps the persisted UI state, inline SVG and the light and dark themes.

## References

- `project/pm.py`, `project/index.html`
- `project/ROADMAP.md` for the initial order to seed `plan.json`
