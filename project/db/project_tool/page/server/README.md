# State server

Walk the db tree, expose it as JSON and serve deliverables.

## Details

`collect_state` walks epics, tasks and subtasks, parses the README title and brief, reads `progress.json` and lists deliverable files with a summary of any JSON test report. `pm.py check` validates the tree: titles, briefs, statuses, kinds, dependency paths and the required deliverables of completed items.
