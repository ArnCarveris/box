# Engine features Ore Else needs

The engine gaps the Ore Else survey found: layered input with consumption and drag, a second tonemap operator, cloud shadows, world anchored UI elements, and an audit that files any gap the port still hits.

## Details

Ore Else was written against flecs-engine and uses a few things box does not have yet. Each is a
proper framework feature with its own tests and scene, not a game specific hack, so the port stays a
consumer of the public API. The audit task runs first and keeps a table from every flecs-engine
component and call the game uses to its box equivalent; a row without one becomes a task in this
epic.

Verification: Unit tests and visual scenes per feature like every other epic, and the audit table as a deliverable
with no open rows once the port is done.

## References

- design/DESIGN.md
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/README.md
- project/db/ore_engine/README.md
