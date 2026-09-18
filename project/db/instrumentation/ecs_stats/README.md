# ECS statistics

The flecs stats addon wired into the world and exposed through the existing component interface.

## Details

Import the stats addon, collect world, system and query statistics at a configurable period and expose them as components so the command layer, the REST API and the status page read them without new endpoints. Collection is enabled by a component like the other monitors. The period and the retained window are fields, not compile time constants.

Verification: unit tests check that statistics are collected, that enabling and disabling works and that values move in the expected direction under a synthetic load. A command based integration test reads them from a running instance and keeps the log.
