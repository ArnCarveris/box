# Build setup

bake3 package with flecs, wgpu-native, glfw and stb bundles building on macOS.

## Details

The root `project.json` declares the `box` package. flecs is a cmake bundle from the local repository, wgpu-native v29 is a cargo bundle, glfw 3.4 a cmake bundle on Darwin only, and stb is header-only. The Emscripten target uses the `emdawnwebgpu` port instead of wgpu-native and links with `-sDEFAULT_TO_CXX`.

`apps/box_app` is the executable and `test/core` the first test project. Modules use CamelCase identifiers (`BoxSurface`) so flecs derives `box.surface` as the script path.
