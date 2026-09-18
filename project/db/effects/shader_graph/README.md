# Post process graph

A data driven post process graph per view: effect entities ordered into passes with shared intermediate targets.

## Details

An effect is an entity with a parameter component and a Shader; a view lists the effects it runs and their order. The graph allocates and reuses intermediate targets by size and format, resolves the inputs an effect declares such as color, depth, normals and motion vectors, and only allocates a target when an enabled effect needs it. Enabling or disabling an effect entity reconfigures the graph without restarting the renderer, and the effects toggle on View disables the stack.

Verification: unit tests cover graph construction, ordering, target reuse, input resolution and enable and disable. A visual scene toggles a trivial effect and compares captures, and a benchmark reports the cost of an empty graph against a graph with every effect enabled.
