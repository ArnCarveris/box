# Shader component

WGSL shaders authored as files and loaded by a Shader component, with includes and macros.

## Details

A Shader component holds a path to a WGSL file and a macro map; setting it compiles the shader and creates or replaces the pipelines that use it, which makes shader reloading a component write. A small preprocessor resolves includes so shaders can share code, and expands macros so one source serves variants such as with and without material layers. The existing embedded mesh shader moves into a file under etc/shaders and is loaded through the same path, on native and on wasm where the files are embedded.

Verification: unit tests cover parsing, include resolution, cyclic include detection, macro expansion and error reporting with a file and line. A visual test changes a macro at runtime and captures the two results, proving the single abstraction works end to end.
