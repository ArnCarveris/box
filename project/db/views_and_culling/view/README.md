# View component

A View entity binds a camera to a render target and carries the toggles that decide what a frame draws.

## Details

Add a box.view module with a View component holding the camera entity, the target surface or texture, a clear color, a priority that orders views in a frame and enable flags for shadows, effects and material layers. The extract system produces one view block per View entity instead of the single implicit camera, and the renderer runs one pass group per view into its target. Views without a camera are skipped and a scene without any view keeps the current default so existing scenes still render. Depth targets stay reverse Z with an infinite far plane as scene_core established.

Verification: a new test/view project covers view extraction, ordering, defaults, missing camera handling and depth precision at large distances. A visual integration scene renders the same world through two views with different cameras, and a capture test proves a view rendering into a texture target produces a different image than the main view.

Introduces View and its private ViewImpl; the layer lists, shadow settings, effect stack and material layer toggle are added by the tasks that own them.
