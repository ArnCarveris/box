# Normal map tangent frame

The screen space tangent frame is derived for a y up framebuffer, so under WebGPU both tangents are negated and bumps read lit from the wrong side.

## Details

`perturb` in the mesh shader builds the cotangent frame from screen space derivatives of the world position and the UVs. That derivation assumes the framebuffer y axis points up as in OpenGL; WebGPU's points down, which negates both tangents. The image row flip of the orientation fix happened to correct the vertical direction, so today bumps are lit correctly vertically and inverted horizontally. Fix the frame for WebGPU's convention and settle the normal map convention as OpenGL style, green pointing along +v, in the generator and the documentation.

Verification: a visual scene with a normal mapped box showing a single large hemispherical bump next to a real hemisphere of the same size, lit by a sun from the upper left, must show both lit on the same side horizontally and vertically; the textured scene is re-rendered and checked by eye. A unit check of the cotangent frame on the CPU with a known surface and screen orientation would be ideal if the math is factored into a shared header; otherwise the visual check is the verification.

## References

- `src/renderer/shader.c`, `perturb`
- `project/db/textures/uv_orientation/deliverable/README.md`
