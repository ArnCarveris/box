# Box
A framework for building 2D/3D games written with flecs. This is the original design brief for the project, and the information in this document may be incomplete and out of date.

- You will build a 2D/3D framework for building games
- The framework will be Flecs-native
- The framework must build for macOS and wasm
- The project will use:
    - cglm for math
    - glfw for window management
    - wgpu-native for rendering (on Desktop, with native webgpu in browser)
    - stb_image and resvg for image loading
- The framework shall be implemented in C

## Data driven
- Flecs is the datamodel for everything (configuration, assets, scene).
- The entire API surface will be changing framework component values + hooks/observers for side effects.
- All public framework components shall have reflection so they can be set from script.
    - Use `ECS_STRUCT` / `ECS_ENUM` macros where possible
- Flecs script is the primary storage format for configuration, assets and scenes.
- Component changes should lead to the framework reconfiguring itself to make the new state "true"
- No features shall be created that require dedicated public native functions
- This will ensure that everything can be created through script files, and everything is accessible through the REST API

## Architecture
- The project will be multithreaded:
    - The main thread will run flecs systems
        - No other thread will have access to the Flecs world
    - A render thread will render using webgpu
        - No other thread will interface with webgpu
        - Communicates with main thread through a render packet
            - At most N packets can be in flight, and memory should be reused across packets
            - So no continuous dynamic memory allocations
    - A separate job system will handle long running tasks
        - IO operations
        - Asset processing
    - A boxrun app will be used as the main application to work with the framework

## Renderer
- Shaders shall be authored in wgsl files, and loaded by a Shader component that contains a path to the file
    - This shall be the single abstraction for all shaders in the engine
    - Shaders shall be able to include each other, and use macros
- The framework will support cascading shadows
- The framework will support Hi-Z occlusion culling (no artefacts with fast moving cameras)
- The framework shall use inversed Z for improved depth precision

## Performance monitoring
- The project will have a set of test scenes
- Each scene must run at 30, 60, 90 or 120 FPS
    - FPS measurements must be taken with a moving camera
    - The camera will move in a circle around the scene, while targeted at the center of the scene
    - The camera will fit the bounding box of the scene
    - All frames must fall within 10% of the requested FPS
        - A frame that falls outside of the requested FPS will throw an error
        - For a regression test to pass, there must be no errors

## Visual regression testing
- The engine will have a visual regression test suite for testing rendering features
- Copy the approach of ../flecs-engine, which generates a baseline and reports deviations on a webpage
- These scenes are not monitored for performance and use a static camera

## Regression testing
- Each feature shall be tested rigorously
- Avoid writing tests for non-existing requirements
- Avoid writing tests that encode internal behavior

## Agent based development
The framework will be optimized for agent-based development, meaning:
- Each headless process shall create a folder in /tmp/box/pid with temporary files
- It shall have a --headless operation
    - Headless mode requires specifying a --timeout or --frames parameter to prevent long-running processes
        - --timeout infinite for long-running processes like playtesting
    - Headless mode shall accept a --cpu-budget parameter. If the instance exceeds the budget, it shall insert a proportionally large sleep
    - If no --cpu-budget is provided, 100% shall be assumed
    - A new headless instance shall perform a check on startup whether the CPU is oversubscribed (based on % and number of cores)
        - If so, it will not start and report an error

## Input
- Uses the flecs.script.event module
- Look at box_old project, ore_else project for how inputs are mapped to actions

## Scenes
- A scene is an isolated space, not just a render filter
    - Lights, picking, collision and spatial indices are scoped to a scene
    - Entities in different scenes never interact, even when their positions overlap
- The renderer shall not walk the hierarchy each frame to find the scene of an entity
    - Scene membership is cached by queries (with up(Scene)) where possible
    - Batches and culling are partitioned by scene
- Nested scenes: an entity belongs to its nearest Scene ancestor
- Many use cases shall be possible with scenes: UI overlays, item previews, minimaps, first person weapon views, picture in picture
- The only cross-scene reference: an entity in one scene can be positioned by projecting an entity from another scene through a view
    - Used for world-anchored screen space UI (nameplates, markers)

## UI
- There is no UI-specific rendering or presentation mode. A UI is built from parts that exist for every scene:
    - Content: a hierarchy of layout and 2D shape entities
    - Scene: the isolated space the content lives in (see Scenes)
    - Camera: what part of the scene is visible
    - View: how and in which order the camera is rendered to a surface
- Screen space UI is a scene of its own, with an orthographic camera, rendered by a view after the view(s) of the 3D scene
    - The camera uses ortho_scale 1, so one logical pixel is one point and the UI keeps its size when the surface is resized
    - The view does not clear, and has lighting, shadows, culling and post effects disabled
    - A HUD, a menu and a debug overlay can each be their own scene and view, stacked in view order
- World space UI is the same content placed in a 3D scene, converted from logical pixels to world units with Scale
- Render-to-texture UI: a Surface with a texture renders the view of a UI scene, and a material samples the texture
    - Only for cases that need it (curved screens, effects on UI). Prefer direct rendering
- World-anchored screen space UI (nameplates, markers) is placed by projecting an entity of the 3D scene through its view
- Text and shapes shall remain crisp at any scale and distance

### Layout
- Layout works the same in every scene, it only knows logical pixels and the hierarchy
    - An element lays out in the local space of its parent, origin centered, y up
    - Rows and columns place children in OrderedChildren order
- Sizing
    - Fit: an element with Layout fits its content plus padding, any other element fits its shape (Rect, Circle)
    - Grow on the main axis of the parent takes a share of the free space, weighted by value (0 counts as 1)
    - Grow on the cross axis, or in a DirectionNone parent, stretches to the content box of the parent
    - An element without LayoutItem fits on both axes
- Placement
    - In a row or column, the parent Layout aligns its children on both axes. h, v, x, y of a LayoutItem are ignored
    - In a DirectionNone parent, a LayoutItem anchors the element to an edge or the middle of the content box,
      then offsets it by x, y. Children without LayoutItem keep their Position; one without a Position
      gets one at the origin of the parent, so it has a place and is drawn
- Ownership
    - The layout writes Position x and y of every element it places, never z
    - A Rect is the input of a fitting element without Layout. On every other element the layout writes
      LayoutBounds into the Rect, so a background follows the computed size

### Input
- Picking casts a ray from the camera of a view into its scene, so screen and world space UI are picked the same way
- Views are picked in reverse render order. An event not handled by a view goes to the next one
- Hit testing uses LayoutBounds, not rendered output
- World space UI occluded by scene geometry shall not receive input
- Render-to-texture UI is picked by mapping the hit UV to a ray through the views of the texture's surface
- UI input shall work with the input commands of the command interface

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

## Materials:
The material system shall support:
- Roughness
- Metallic
- Emissive
- Opacity
- Alphablending
- PBR textures (albedo, emissive, roughness, normal)
    - Texture tiling/wrapping
    - Texture transforms
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
- WorldTransforms for objects with DynamicTransform is uploaded each frame in instanced batches
- Material data for objects with DynamicMaterial is uploaded each frame in instanced batches
- Static content is uploaded on change
    - Use a scatter approach where a buffer with changes is uploaded to the GPU, with a shader that populates the changed slots
- Look for inspiration on how to instance objects at flecs-engine

## Controllers
- The framework shall have a default camera controller (WASD for moving in XZ, QE for Y, mouse for direction)
- The framework shall have a first person controller (WASD for moving, space for jump, ctrl for crouch, F for freeflight)
- Controllers shall collide automatically with objects
    - Make sure it's fast- no O(n) walks of all objects in scene
    - Verification: navigate through scene with 1 million objects
        - FPS should remain at 60 FPS
- Controllers should use the input system
