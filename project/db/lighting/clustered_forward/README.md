# Clustered forward lighting

A froxel cluster grid so many point and spot lights can light dynamic geometry in one forward pass.

## Details

The renderer builds a cluster grid per view, assigns lights to clusters in a compute pass and the surface shader iterates the lights of its cluster. Light data arrives from extract as a per view array built from DirectionalLight, PointLight and SpotLight entities and restricted by the layers of the view. Lights are culled by range against the frustum before clustering, and the grid dimensions and the maximum lights per cluster are view settings.

Verification: unit tests cover light extraction, range culling, layer restriction and cluster assignment on the CPU side. A visual scene with several hundred colored point lights over a primitive city is captured, and a benchmark with 1000 moving lights reports the clustering and the shading cost.
