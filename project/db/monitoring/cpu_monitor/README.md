# CPU monitoring

Process and machine CPU usage published as a component and shared with the CPU budget.

## Details

A CpuMonitor singleton samples process and system CPU usage per core over a window and publishes it; adding the component enables sampling, so it costs nothing when it is absent. The existing cpu-budget sleep and the oversubscription check at startup read the same source instead of measuring separately. Values are readable over the REST API and the terminal like any other component.

Verification: unit tests cover sampling, the moving average, enabling and disabling and the budget calculation against synthetic loads. A command based integration test reads the component from a running instance and keeps the log.
