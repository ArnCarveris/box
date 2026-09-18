# System monitoring

Per system and per phase ECS frame timings published as components.

## Details

A SystemMonitor singleton measures per system time through the flecs monitoring hooks and publishes the slowest systems, the per phase totals and the frame breakdown. Measurement is off by default and turning it on must not change behaviour beyond its own cost. The data feeds the frame spike harness so a spike can be attributed to a system rather than guessed at.

Verification: unit tests register a system that burns a known amount of time and check the attribution, and cover enabling, disabling and the phase totals. A benchmark reports the overhead of having monitoring on.
