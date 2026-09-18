# Clustered forward lighting

Extract builds `box_packet_t.lights` from `PointLight` and `SpotLight` entities:
position and spot direction from `WorldTransform`, linear color from `Rgb`, cone
cosines from the half angles, culled by range against the six frustum planes.
`ClusterSettings` (16 x 9 x 24 clusters, 64 per cluster) is a singleton in
`box.light`, copied into `box_view_t` with the camera near and far as its range.

The renderer uploads the lights to a storage buffer; a compute pass
(`src/renderer/lights.c`, `cs_main` in `shader.c`) intersects each light sphere
with the view space froxel of its cluster and writes a light index list plus an
offset and count per cluster. The mesh fragment shader finds its cluster from the
view space position and shades those lights with the sun path's shading, windowed
inverse square falloff and a squared cone term. Grid buffers and bind groups are
rebuilt only when the settings or the light capacity change.

Numbers: 18 tests in `render.Lights` pass. Extracting 1000 moving point lights
takes 6.8 us (145 M lights/s), 5.6 us when all are culled; `lights_city` (325
boxes, 324 point lights, 6 spots) runs at 0.20 ms mean frame time.
`Extract.static_1_changed` regressed 11% (352 -> 391 ns) from the light query.

Limitations: one view, layer restriction waits for the views epic; the froxels
assume a perspective camera; lights past the 64 per cluster or 4096 per packet
are dropped; directional lights keep the single sun path.
