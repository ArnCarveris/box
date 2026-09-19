# Stress test base

The --stresstest flag building the late game base and advancing the clock.

## Details

Port stresstest.c: two wall rings with a turret ring behind them, a drill on every deposit, a solar
farm sized to demand, a rocket pad, factories and drone pads, a stocked inventory and the clock at
the given minutes, rebuilt on restart.

Verification: A unit test that the base builds, bench scenes at 30 and 45 minutes through tools/integration.py
bench, spike free at the standard budget.

## References

- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/src/stresstest.c
- test/integration/bench
