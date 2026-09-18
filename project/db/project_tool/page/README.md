# Project status page

Python server that walks project/db and serves a board and timeline view.

## Details

`pm.py serve` exposes the tree as JSON on `/api/state` and serves `index.html`, which polls it every five seconds. The board shows the selected epic's tasks as cards with status, kind, duration, dependencies, subtasks and the latest test totals. Clicking an item opens a panel with its README, deliverables and inline screenshots.

The timeline lists every epic, task and subtask as a row with a bar from start to end time, an in-progress bar extends to a red "now" marker. It scrolls horizontally, zooms and supports light and dark themes. Deliverable files are served from `/deliverable/<path>` and only from `deliverable` folders.
