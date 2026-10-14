// Shadow pass: draws the shadow casters of a view into one cascade of its shadow map, depth
// only. The instance layout is the one of the forward pass; only the model matrix is read.

struct Cascade {
    matrix: mat4x4f,
};

@group(0) @binding(0) var<uniform> cascade: Cascade;

struct VertexIn {
    @location(0) position: vec3f,
    @location(3) model_0: vec4f,
    @location(4) model_1: vec4f,
    @location(5) model_2: vec4f,
    @location(6) model_3: vec4f,
};

@vertex fn vs(v: VertexIn) -> @builtin(position) vec4f {
    let model = mat4x4f(vec4f(v.model_0.xyz, 0.0), vec4f(v.model_1.xyz, 0.0),
        vec4f(v.model_2.xyz, 0.0), vec4f(v.model_3.xyz, 1.0));
    return cascade.matrix * (model * vec4f(v.position, 1.0));
}
