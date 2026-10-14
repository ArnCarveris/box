// Forward pass: draws shape meshes with a sun light and a sky ambient.
// Instance normals use the inverse transpose of the model rotation for non-uniform scale.

struct View {
    view_proj: mat4x4f,
    camera: vec4f,
    light: vec4f,
};

@group(0) @binding(0) var<uniform> view: View;

struct VertexIn {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) model_0: vec4f,
    @location(4) model_1: vec4f,
    @location(5) model_2: vec4f,
    @location(6) model_3: vec4f,
    @location(7) normal_0: vec4f,
    @location(8) normal_1: vec4f,
    @location(9) normal_2: vec4f,
    @location(10) color: vec4f,
};

struct VertexOut {
    @builtin(position) pos: vec4f,
    @location(0) world: vec3f,
    @location(1) normal: vec3f,
    @location(2) color: vec4f,
};

@vertex fn vs(v: VertexIn) -> VertexOut {
    let model = mat4x4f(v.model_0, v.model_1, v.model_2, v.model_3);
    let normal_matrix = mat3x3f(v.normal_0.xyz, v.normal_1.xyz, v.normal_2.xyz);
    let world = model * vec4f(v.position, 1.0);
    var o: VertexOut;
    o.pos = view.view_proj * world;
    o.world = world.xyz;
    o.normal = normal_matrix * v.normal;
    o.color = v.color;
    return o;
}

@fragment fn fs(f: VertexOut) -> @location(0) vec4f {
    let n = normalize(f.normal);
    let l = normalize(view.light.xyz);
    let diffuse = max(dot(n, l), 0.0);
    let sky = mix(vec3f(0.08, 0.07, 0.06), vec3f(0.18, 0.22, 0.3), n.y * 0.5 + 0.5);
    let lit = f.color.rgb * (sky + vec3f(1.0, 0.96, 0.9) * diffuse);
    return vec4f(lit, f.color.a);
}
