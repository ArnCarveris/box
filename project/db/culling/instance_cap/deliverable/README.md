# Renderer instance cap

## Built

The device was created without `requiredLimits`, so it took the WebGPU defaults:
a 256 MiB buffer and a 128 MiB storage binding, which is 860369 instances of 156
bytes. A scene past that either binds an oversized static buffer, which is the
invalid bind group, or doubles an instance buffer past 256 MiB, which is the
refused `wgpuDeviceCreateBuffer`. `box_device_limits` now asks the adapter for
its limits and requires them (27 GiB and 4 GiB here), `box_device_instances`
turns them into an instance ceiling, and the static, dynamic, visible and caster
buffers double up to that ceiling instead of past it; the reservations of the
cull and shadow passes clamp to it, so a scene beyond the hardware drops
instances instead of aborting. The bind group caches of the cull, occlusion and
shadow passes already key on their buffers and rebuild on growth. The static
slots of a packet are now written one run of neighbouring slots at a time, in
chunks of 4096, instead of one queue write per instance.

## Verified

`render.Instances.static_1m_renders_a_frame` extracts a million statics and
renders a frame headless; on the old renderer it aborts with the invalid bind
group. 537 cases over `test/render`, `test/view` and `test/core`, lint 0, all 63
baselined visual scenes bit identical (`picking_hover` is another task's scene
and differs the same way without this change). `instances_1m.flecs` draws 988k of
a million instances at 116 fps, mean 8.6 ms, p99 27 ms, no spikes; coalescing the
static writes took its load frame from 9236 ms to 67 ms.

## Left out

The instance ceiling is a clamp, not paging: a device whose storage binding stops
at 128 MiB still draws at most 860369 instances per buffer. Chunked bindings are
the follow up. The 300k reproducer the task asked for does not fail, because the
cap sits near 860k; the test uses a million.
