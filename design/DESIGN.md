# Box
A framework for building 2D/3D games written with flecs.

## The task at hand
- You will build a 2D/3D framework for building games with flecs
- The framework will be built using webgpu with wgpu-native
- The framework shall be built with bake3
    - performance verification shall be done with `--cfg release`
- The framework must build for macOS and wasm
- The framework shall be implemented in C
- You shall use the flecs repository at ../../flecs
- If you encounter bugs or missing features in Flecs or Bake3 (including the ones described below), you may implement those directly in the repository. Commit each feature/fix separately.

## Why this project
You will find an existing flecs-engine project on this machine, but it has issues:
- It was a prototype project to test how far Flecs could used for a "real" engine
- Many features got bolted on later on that weren't considered in the initial design
- It is based on an older version of Flecs that still had implicit defer mode, which complicates the code

This project proposes a data-driven framework that is focused on primitive procedural geometry, and the features needed to make that work at scale. This is our niche, and something we can excel at with flecs script templates in hand.

You may look at the other project for how their build configurations is setup, but I advice against reading flecs-engine code, as this will risk contaminating your context with outdated designs.

## Project management
You shall build a small project management tool:
- It shall have a web page for exploring the current state of the project
- The work shall be broken up into epics, tasks and subtasks:
    - An epic is a large feature ("command API")
    - A task is a single concrete unit of work that can be used and tested ("input system of command API")
    - A subtask is a unit of work that can be handed off to an agent
- The tool and state of the project shall be stored in a project folder
    - The project state shall be stored under project/db
        - Epics shall be stored under project/db/<epic_name>
        - Tasks shall be stored under project/db/<epic_name>/<task>
        - Subtasks shall be stored under project/db/<epic_name>/<task>/<subtask>
- Each task shall have a project/db/<epic_name>/<task>/deliverable folder
    - This shall contain a JSON file with the unit test results ran after the project
        - Extend bake3 with a feature that outputs this JSON file. It should include tests ran, test result and execution time.
        - The task shall only run part of the test suite that is affected by the task
    - Deliverables can include screenshots, benchmark results, research
    - If the result is research, keep text brief so as not to waste context of other agents.
- Each epic shall have a project/db/<epic_name>/deliverable folder
    - This shall contain a wasm build of the engine under deliverable/wasm that the inspector page (see below) can load
    - This shall contain a JSON file with the all unit tests (see above)
    - This shall contain integration test results
    - This shall contain a small README.md file (no more than 200 lines) detailing how the epic was completed.
- Each item shall have a README.md in its folder describing its goal. It shall have a:
    - Title
    - Brief description (no more than two sentences)
    - Detailed description (no more than three paragraphs, each no longer than 5 sentences)
    - References (optional)
- Each item shall have a progress.json in its folder describing its start_time, end_time, dependencies, kind (bug, feature, refactor) and status (pending, in progress, complete, discarded)
- The tool shall walk the file tree to retrieve current state and serve up the webpage
- Include a timeline view that shows completed/in progress tasks on a scrollable timeline
- I'll be spending quite a bit of time monitoring this page, so give it a nice modern look/layout
- You are responsible for managing subagents. When starting a subagent, select an appropriate intelligence level to prevent usage from running out. Where necessary, tell agents to use a private local environment (--local-env env_name) where env_name corresponds to the sub task.

## Architecture brief
You must design a high-level architecture, with clean separation of concerns.
- You must organize source files according to this high-level architecture
    - Use flecs modules to organize framework code
- Each file must have a header that clearly describes its intended goal (in no more than 2 sentences)
- Files should not grow beyond 2000 lines, unless there are repeating patterns (such as initializer lists or switch cases) that inflate file length
- Functions should not be more than 200 lines, unless there are repeating patterns (idem)
- Each function shall have a single preceding comment that:
    - has a single line describing what it does
    - has a single line describing what each argument does
    - has a single line describing what the function returns (if anything)
    - see coding_style.c
- Each type shall have a single preceding comment that has a single line describing what the type does
    - has a single line per member/constant describing what it does
- Do not write any other comments. Source code is not the place for architecture, design decisions or bug history. Functions should be short enough to be self-evident.
- Use asserts liberally, and if statements sparsely. Avoid overly defensive code.
- The framework shall be a showcase for Flecs, which means performance is critical
- The framework shall be usable as example code, meaning clear architecture and an emphasis on clarity
- Be on the lookout for "happy coincidences" where a single mechanism can unexpectedly be used for multiple features
- The files under datamodel are non-exhaustive. You may find yourself having to extend the design
    - The files are not meant to be loaded by the framework, they are design documents only
    - Actual types should be defined in native code
- Each change must get its own commit

## Agent based development
The framework will be optimized for agent-based development. This means:
- Each headless process shall create a folder in /tmp/box/pid with temporary files
- It shall have a --headless operation
    - Headless mode requires specifying a --timeout or --frames parameter to prevent long-running processes
        - --timeout infinite for long-running processes like playtesting
    - Headless mode shall accept a --cpu-budget parameter. If the instance exceeds the budget, it shall insert a proportionally large sleep
    - If no --cpu-budget is provided, 100% shall be assumed
    - A new headless instance shall perform a check on startup whether the CPU is oversubscribed (based on % and number of cores)
        - If so, it will not start and report an error
- The framework shall be fully data driven, meaning:
    - Its configuration, scene and assets will exist as entities and components
    - Modifying those entities and components will immediately result in side effects
    - This shall be done through component hooks
    - No features shall be created that require dedicated public native functions
    - This will ensure that everything can be created through script files, and everything is accessible through the REST API (see below)
- The framework shall have builtin performance monitoring:
    - Monitor CPU usage (used to enable CPU monitoring)
    - Monitor ECS systems
    - Monitor GPU timings
    - Monitor ECS statistics (using the flecs stats addon)
- The framework shall have a checkpoint system
    - A checkpoint is a folder with flecs script files with the current state of the instance
    - A checkpoint will iterate all loaded scripts, update them with the latest state, and write them to the checkpoint folder
    - Each headless instance will have its own folder with checkpoint data
    - The folder will be cleaned up when the instance stops
    - Agents will be able to restore to a checkpoint, which means reloading the script files (will require a new flecs ecs_script_reload API)
    - Checkpoints may assume static scenes, and do not need to account for game logic/ECS systems modifying state
    - Modifications made to an entity that is not defined by a script will not be checkpointable

## Command interface
The framework shall expose a command interface that:
- is accessible as HTTP API
- is accessible by pressing ` when in game
- commands shall be implemented as flecs script functions
- to send commands, an agent has to create a terminal (/terminal/new endpoint), which returns a new terminal session handle
- a terminal can be closed with /terminal/close?session=session
- each session shall have its own state
- each session shall have its own /tmp/box/<pid>/session folder
- each command shall be logged
- concise interface/output so no tokens are wasted
- provides the following commands:
    - Entity shell: cd [path], ls [path], touch [path], rm [path], mv [path_from] [path_to]
    - Component operations: [add|remove] [entity] [component|pair], [set|get] [entity path] [component path] [value], clear [entity]
    - Search:
        - query [name] [query]: Create ECS query with specified name
            - iter [query]: Enter iteration mode for query
            - next: return next result for query
            - next [count]: return next result for query
            - done: finish iterating query
            - Any non-next command will exit iterator mode
        - has [entity] [component|query]: Does entity have component, or does entity match query
        - grep [text]: Grep current path (set with cd) for text (search entity names, components of entities in scope, component values)
        - grep [component] [text]: Grep components in current path (set with cd) for text (component values)
        - grep_recur [text]: Same as grep, but recursively grep
        - grep_recur [component] [text]: Idem
    - Help: man [path] (shows content of flecs.doc components for entity)
    - Render operations:
        - capture: captures currently active camera
        - capture [camera]: captures camera
        - capture [camera_0, camera_1, ... ,camera_n]: batch-capture multiple cameras
        - render [entity|prefab|template]: return render of specified entity, prefab or template
        - render functions shall save an image to the /tmp/box/<pid>/session/resources folder, and return the relative file path
    - Log operations:
        - commands: Return all issued commands & results
        - commands [count]: Return last count commands
        - logs [level] [count]: Return last count logs for provided log level (and above)
    - Checkpoints:
        - checkpoint: Create checkpoint, return path to new checkpoint
        - restore: Restore to last checkpoint
        - restore [checkpoint]: Restore checkpoint
    - Flecs scripts:
        - script_read [path]: Read script file
        - script_write [path] [code]: Overwrite content of script file (ecs_script_update)
        - script_grep [text]: Grep for text in script files
        - script_patch [file] [patch]
    - Input:
        - Device layer:
            - key.press     [key, dur?]                 tap; dur defaults to one frame
            - key.down      [key]
            - key.up        [key]
            - key.type      [text, dur?]                one key.press per character, spaced by dur
            - mouse.move    [pos, norm?]                pins the cursor override
            - mouse.sweep   [from, to, steps?, dur?]    queued cursor walk spread over frames
            - mouse.release []                          clears the override, real pointer resumes
            - mouse.button  [button, down]
            - mouse.click   [button, pos?, dur?]
            - mouse.drag    [button, from, to, steps?, dur?]
            - mouse.scroll  [dx, dy]
            - input.reset   []                          releaseAll: empties the queue, clears state
            - input.focus   [bool]                      focus lost path; real input is ignored when false
            - input.wait    []                          block until the queue is drained
        - Action layer:
            - action.press  [action, dur?]
            - action.down   [action]
            - action.up     [action]
            - action.drag   [action, from, to, steps?, dur?]
            - action.list
- The HTTP API shall provide a /resource/[path] endpoint for requesting the content of resources (such as a rendered screenshot)
    - This API shall only return files in the /tmp/box/<pid>/session/resources folder

## Inspector
The framework shall ship a web page for inspecting a running instance:
- It shall connect to a running instance over the HTTP command API (default http://localhost:27750)
- It shall connect to a wasm image of the engine by loading the image in the page and calling an exported request function directly, the same trick the flecs explorer uses for the flecs REST API (`flecs_explorer_request` via `ccall`), so no HTTP server is needed in the browser
- Both transports shall present the same interface to the page: the page code does not know which one it talks to
- It shall have a command console that creates a terminal session on connect, sends command lines, shows the command log and keeps a history
- It shall show an automatically updating render of the active camera at 1 Hz, captured through the command API and fetched as a resource
- The refresh rate shall be configurable and refreshing shall be pausable
- The page shall be a single static HTML file without a build step, usable from a file URL or served by any static file server
- The project management page shall link every epic that delivers a wasm image to the inspector page connected to that image, so the state of the engine as of that epic can be inspected with one click
- The project management page shall also link to the inspector connected to a locally running instance

## Validation
The project shall be validated in the following way:
- A unit test suite for catching regressions. Add a single comment line in each test function that refers back to the epic/task/subtask that is being verified.
    - bake3 will run unit tests
    - bake3 will provide a JSON file with test results (see above) which will can be used as deliverable
- Integration tests:
    - Each integration test is a single flecs script file
    - There are three kinds of testst:
        - Visual screenshot-based tests
            - Deliverable: screenshots
            - Each scene has a stored baseline image; the runner compares the render against it automatically and fails on a difference, so no agent needs to look at screenshots
            - An agent only inspects the scene of the task it works on, and updates that scene's baseline (--update-baseline) when the change is intended; other scenes must keep matching their baselines
        - Command-based tests
            - Deliverable: command log
        - Benchmarks
            - Deliverable: benchmarks (JSON)

## Data driven
- Flecs is the datamodel for everything (configuration, assets, scene).
- The entire API surface will be changing framework component values + hooks/observers for side effects.
- All public framework components shall have reflection so they can be set from script.
    - Use `ECS_STRUCT` / `ECS_ENUM` macros where possible
- Flecs script is the primary storage format for configuration, assets scenes.
- Component changes should lead to the framework reconfiguring itself to make the new state "true"
- Refer to the datamodel folder for the desired interface

## Renderer
- The renderer shall be wgpu-native based
- The renderer shall run on a separate thread
- The game shall communicate with the renderer through a thread safe queue
- The queue shall contain Frame packets which contain all of the work the renderer needs to perform for rendering that frame
- This data is extracted by an Extract system in the main thread
- The main thread does not have access to wgpu, the renderer thread does not have access to Flecs
- This has the potential for being an expensive handoff, think about strategies to reduce allocation pressure and memory usage
- Shaders shall be authored in wgsl files, and loaded by a Shader component that contains a path to the file
    - This shall be the single abstraction for all shaders in the engine
    - Shaders shall be able to include each other, and use macros

## Graphics
- The framework shall be wgpu based, cross-platform, and use bake3 for building
- Materials shall support PBR parameters (metallic, roughness)
- Materials shall support PBR textures
- Materials shall have configurable layers for custom effects (dust, grime, edge wear, scratches)
- Lighting shall automatically be baked by GPU for static lights/static geometry
    - Baked lighting shall include emissive objects
- Non-static lights/static geometry shall use dynamic lighting
    - Dynamic lighting shall use clustered forward rendering
- Image based lighting (from static color or atmosphere, see below)
- Single directional light (used for shadows)
- The framework will support cascading shadows
- The framework will support Hi-Z occlusion culling (no artefacts with fast moving cameras)
- The framework will support a post process effects shader graph
- Supported effects:
    - gtao
    - bloom
        - use approach that efficiently supports large bloom kernels
        - no naive threshold approach
        - with improvement for firefly reduction
    - auto_exposure
    - fog (height based fog)
    - tony (tone mapping, include dithering)
    - ssr
- The framework shall use inversed Z for improved depth precision
- The framework shall support views and layers
    - Layers shall be user-defined, and use fragmenting relationship
    - An entity may be in more than one layer
    - Layers apply to entities, lights
    - A view shall be able to configure which layers it should use (no layers configured = render all)
    - A view shall be able to configure which layers cast shadows (no layers = all cast shadows)
    - A view shall have a field for which camera it renders
    - It shall be possible to toggle effects on a view
    - It shall be possible to toggle shadows and configure shadow parameters on a view
    - It shall be possible to toggle material layers on a view

## Sky
The framework shall have a sky rendering system:
- Atmosphere rendering
    - Sun
    - Moon with phases
    - Stars
- Atmosphere shall be usable as IBL
- Atmosphere can be synchronized with fog
- Time of day system that synchronizes with directional light
- Weather system:
    - Overcast (plain color)
    - Fog
    - Rain
    - Synchronizes with atmosphere

## Materials:
The material system shall support:
- Roughness (reflection from IBL, SSR if enabled)
- Metallic
- Emissive
- Opacity
    - Combine with roughness for "milk glass" effect, or blurred UI panels
    - Milk glass is transmission, not an alpha overlay: the opaque scene is snapshotted into a filtered mip pyramid, sampled at the refracted screen position with a mip level driven by roughness, and the transmitted color enters the PBR shading (specular from lights and environment on top), see flecs-engine's render_transmission and pbr_transmission for the reference
    - A roughness sweep at fixed opacity must show clearly distinct blur levels
- Alphablending
- PBR textures (albedo, emissive, roughness, normal)
    - Texture tiling/wrapping
    - Texture transforms
- Material layers for custom effects
    - Must be extendable per project
    - Must be enableable per material
    - Are global in a scene, but applied per material
    - Included:
        - dust
        - edge wear
        - grime
        - wetness
- Tint
    - Applied recursively to children on change
    - Tint is {r, g, b, a} where a is the tint strength: the surface color is mixed toward the tint color by a; it is not transparency (that is Opacity)
    - Tints compose down the hierarchy: a child's tint applies on top of its ancestors' result (blue at 50% on a parent plus black at 50% on a child darkens the grandchild's light blue), for ChildOf, Parent and mixed trees alike

## Content
- Content shall be authored primarly with flecs script templates
- Content shall be authored primarily by creating hierarchies of scalable primitive shapes:
    - Shapes will be processed as instanced batches
    - Create an extendable framework where projects can easily plugin their own scalable shapes
    - Included (3D): Mesh3, Box, Quad, Triangle, RightTriangle, TrianglePrism, RightTrianglePrism, NGon, Sphere, HemiSphere, IcoSphere, Cone, Cylinder, Torus, Capsule, Tube, Frustum
    - Included (2D): Rect, Circle, Border (applied to Rect, allows for specifying border radius), Line, Image (shows area of texture)
    - An entity with "Sphere: {segments: 6, radius: 10}" shall select a cached/resuable mesh with 6 segments (radius shall not be baked into the mesh)
    - Make sure shapes have UVs
- The framework shall support Parent and ChildOf hierarchies
- Content may be marked with DynamicTransform
    - This means the framework should assume that transform changes each frame
    - Entities without DynamicTransform are considered static:
        - They should not be transformed each frame
        - They should not be uploaded to the GPU each frame
        - They should only be transformed/uploaded when Position/Rotation/Scale changes (use observer)
        - A single change in a static entity should not cause all static entities to upload
        - Verify performance with a stress test:
            - A scene with one 100k static entities
            - 10k moving entities
            - Each frame between 1-1000 static entities move
            - FPS should stay above 60
        - Incorporate this in the shadow design as well, as this is a likely bottleneck
    - Make sure transforms work correctly with subtrees (use extensive unit tests)
- Content may be marked with DynamicMaterial
    - This means the framework should assume that material-related properties may change each frame
    - Same applies as above
    - Verify with stress test:
        - A scene with 100k static entities with 1000 materials
        - 10k entities with changing material parameters
        - Each frame between 1-1000 static entities change parameters
        - FPS should stay above 60
- The framework shall have a reusable spline framework
    - Splines shall be usable for creating things like roads
        - This shall be done in vertex shaders to avoid expensive mesh generation
    - Splines shall be usable for things like animation, particle systems

## Particles
- Particle properties: position, velocity, acceleration, lifetime/age, size, color, opacity, and optional rotation.
- Configurable emission rate and maximum particle count.
- Configurable emitter shape/area and particle spawn distribution.
- Randomization of particle direction, speed, size, color, and lifetime.
- Support for visual changes over lifetime, such as fading, color transitions, and scaling.
- Support for configurable forces, such as gravity, wind, or drag.
- Allow different particle appearances, such as circles, sprites, or simple textures.
- Support blending/transparency for effects such as smoke, fire, sparks, or magic.
- The emitter should support continuous emission and short bursts.
- Provide controls to start, stop, pause, and reset the effect.
- Verification: 10 million particles at 60 FPS

## Interaction
- The framework shall calculate AABB for prefab/template roots
- The framework shall assign colliders to objects
    - Cheap where possible, e.g. a sphere object shall use a cheap SphereCollider
- The framework shall provide object picking integration with the flecs script event module picking (e.g. "await on.drag(this)")
    - See script playground for an example
    - Support event bubbling
    - Make sure it's fast- no O(n) walks of all objects in scene
    - Verification: pick object in scene with 1 million nested objects (50k top level)
        - Should take less than 1ms

## Input
- Two layers: a device layer (raw key/mouse state) and an action layer (named actions bound to keys)
- Device layer:
    - An `Input` singleton holds per-frame state for 128 keys, left/right mouse buttons and a text char buffer
    - Each key/button has `pressed` (went down this frame), `state` (held) and `current` (raw OS state)
    - Mouse is stored as window coords, per-frame delta, view-centered coords, normalized (-1..1) coords and accumulated scroll
    - Key codes are ASCII where possible; non-printable keys use spare codes; mouse buttons are codes 128/129 so they bind like keys
    - A single OnLoad system resets per-frame state, then applies OS events (GLFW natively, HTML5 canvas callbacks on web) and injected events
    - Real input is ignored while the window is unfocused or minimized. Losing focus clears all key/button state. Regaining focus does not cause a phantom press or cursor jump
- Action layer:
    - Actions are entities with an `InputAction` component (description)
    - An `InputMap` singleton maps keymasks to actions (must be authorable from script, like `keys.W: { actions.Forward }`)
    - A keymask is a u64: low 32 bits are the key, high bits are 8 modifier bits (left/right shift, ctrl, alt, super)
        - Family masks (`Shift`, `Ctrl`, ...) match either side
        - A modifier key on its own binds as a pure modifier mask (`keys.ShiftLeft: { actions.Run }`)
    - Key names are exported as script constants under `keys` (`keys.W`, `keys.MouseLeft`, `keys.Ctrl`)
    - UI widgets can emit an action on click, so a button and a key binding share one action. Any code can emit an action without a key
    - Integrate with script event framework, so that scripts can do:
    ```
    async {
        await on.pressed(Forward)
        // ...
    }
    ```

## Controllers
- The framework shall have a default camera controller (WASD for moving in XZ, QE for Y, mouse for direction)
- The framework shall have a first person controller (WASD for moving, space for jump, ctrl for crouch, F for freeflight)
- Controllers shall collide automatically with objects
    - Make sure it's fast- no O(n) walks of all objects in scene
    - Verification: navigate through scene with 1 million objects
        - FPS should remain at 60 FPS
- Controllers should use the input system

## Responsiveness
- The framework shall not cause frame spikes during large workloads
    - It is OK for the base framerate to be low if the scene is truly too large for the hardware to handle
    - It is not OK for the same content to cause frame spikes that cause the game to become unresponsive
- Scenes shall be loaded incrementally
    - See 355b18260b43b1f2bf672fccb9d3e0726f3682aa in flecs repository
    - Only applies to templates- that is OK
    - Non-template objects shall load immediately
        - Still useful for things like loading in the ground first
    - Load templates close to camera first
- For other large tasks, implement a job system
    - Use this for incremental SVG rasterization
    - Use this for light baking
    - Prioritize SVG/light baking near camera
- Script loading performance is not a concern

## UI
- Retained UI framework
- Implement as view with orthographic camera
- Entities inside parent with "Ui" component will be treated as UI
    - Inside a UI tree children are always rendered on top of parents, so no zfighting
    - Support BoxShadow inside of UI trees
    - Use same flecs script event framework for catching UI events
    - Entities inside of UI trees can use layout components
    - Inside a UI tree transparency works differently:
        - Applying transparency to a parent will apply it as if it is applied to the rendered result
            - No ghosting of elements on the panel, not even if they have their own transparency settings
            - Probably means transparent UI panels have to render to textures
- Reuses 2D shapes, Position, Rotation, Scale components
- Widgets interactions (such as hover) will be entirely defined in flecs script
- The UI library shall (therefore) not contain any widget-specific logic
- The UI library shall provide utility helpers for building text widgets
    - These utilities should be usable from flecs script

## Transitions
- The framework shall support transitions for all authorable continuous values
    - Examples: Position, Rotation, Rgb, Tint, Emissive
- The author-facing component shall always be set to the value that the user sets it to
- The transition data is stored on the message that gets sent to the renderer
- The renderer thread applies in progress transitions and computes the actual values
- Make sure that overhead is only incurred for objects that actually have transitions
- When a new value is assigned, a new transition should start
- Verification: have a scene with 10k entities, each with 10 transitions

## LOD
- Implement automatic hierarchical LOD for scenes composed primarily of entity trees of primitive shapes.
- Use screen-space error rather than fixed distance thresholds, with support for perspective/orthographic cameras and hysteresis to prevent LOD thrashing.
- Use the authored entity hierarchy as the primary LOD hierarchy. A subtree may be replaced by a cheaper representation, and replaced descendants shall not be traversed or rendered.
- Runtime LOD selection shall operate on renderer-owned hierarchy data and must not require visiting every renderable entity each frame.
- Automatically reduce primitive tessellation based on projected size/error for shapes such as spheres, cylinders, torus, capsules, tubes and splines.
- Automatically prune visually insignificant descendants, such as trim, window frames, bolts and railing details, when their projected contribution falls below the configured error threshold.
- Automatically generate HLOD proxies for suitable subtrees. Prefer reduced primitive representations; fall back to merged/simplified meshes and optionally impostors for very distant objects.
- Evaluate meshoptimizer for error-bounded mesh simplification rather than implementing a mesh simplifier from scratch.
- Optimize LOD for total rendering cost, including instance/object count, triangle count, draw/material groups and GPU memory.
- If the authored hierarchy is unsuitable for LOD, optionally create renderer-only spatial clusters without modifying Parent/ChildOf relationships.
- Generated LOD data shall be cached and incrementally invalidated. Rigid root movement shall not regenerate proxies; relevant descendant changes shall invalidate only affected proxies.
- LOD generation shall use the job system, prioritize visible/nearby content, never block rendering, and avoid frame spikes.
- Views shall expose data-driven LOD quality/error settings. Shadows may use more aggressive LOD settings than the primary view.
- LOD shall preserve authored ECS identity; rendering proxies must not affect gameplay entities, picking or collision semantics.
- Integrate LOD hierarchy traversal with frustum and Hi-Z occlusion culling, so rejected parent nodes prevent traversal of their descendants.
- Add debugging/statistics and tests for LOD selection, primitive reduction, detail pruning, proxy generation/invalidation and hierarchy traversal. Verify with a ~4 million object city scene (representative geometry, not 4 million objects in the scene root) that traversal cost scales with visited LOD nodes rather than total leaf entities and does not introduce frame spikes.

