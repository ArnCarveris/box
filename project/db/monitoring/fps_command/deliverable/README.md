# fps command

`fps` prints `<n> fps, mean <x> ms, worst <y> ms` from the window the monitor
publishes on `FpsOverlay`; `fps on` and `fps off` set `FpsOverlay.enabled` on the
active view, or on the singleton when no view has been extracted yet, keeping
the corner, scale and opacity of the panel. The first call creates the component
off, because nothing is sampled while it is absent, and reports the frame the
world is in until the window has one. An argument that is neither `on` nor `off`
is a one line error. Registered under `box.command.commands` like every other
group, so `help` lists it and the inspector reaches it over the terminal.

Verified: 10 cases in `command.Fps`, 209 in `test/command`, plus `test/monitor`,
`test/render` and `test/view` green, lint 0, and `fps.log` holds the on, off,
print and error results of the new command scene; every command scene passes.
Fixed along the way: the monitor dropped the first frame of its window because
the flecs frame counter only advances at the end of a frame.

Left out: the inspector render pane was checked through the headless capture of
the overlay task rather than in a browser.
