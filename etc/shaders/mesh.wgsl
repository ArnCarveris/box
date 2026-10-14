// Forward pass: draws shape meshes with a physically based surface, a GGX distribution with
// height correlated Smith visibility and Schlick Fresnel, lit by the light inputs of the view.
// Instance normals use the inverse transpose of the model rotation for non-uniform scale.
// The model matrix is affine: the w of its first columns holds metallic and roughness, the w
// of the normal columns holds the emitted color.

// BlendMode of the pipeline: the output is shaped for the blend equation it is drawn with.
override blend_mode: u32 = 0u;

const PI: f32 = 3.141592653589793;
const MIN_ROUGHNESS: f32 = 0.045;

struct View {
    view_proj: mat4x4f,
    camera: vec4f,
    light: vec4f,
    light_color: vec4f,
    ambient_sky: vec4f,
    ambient_ground: vec4f,
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
    @location(3) emissive: vec3f,
    @location(4) material: vec2f,
    @location(5) uv: vec2f,
};

@vertex fn vs(v: VertexIn) -> VertexOut {
    let model = mat4x4f(vec4f(v.model_0.xyz, 0.0), vec4f(v.model_1.xyz, 0.0),
        vec4f(v.model_2.xyz, 0.0), vec4f(v.model_3.xyz, 1.0));
    let normal_matrix = mat3x3f(v.normal_0.xyz, v.normal_1.xyz, v.normal_2.xyz);
    let world = model * vec4f(v.position, 1.0);
    var o: VertexOut;
    o.pos = view.view_proj * world;
    o.world = world.xyz;
    o.normal = normal_matrix * v.normal;
    o.color = v.color;
    o.emissive = vec3f(v.normal_0.w, v.normal_1.w, v.normal_2.w);
    o.material = vec2f(v.model_0.w, v.model_1.w);
    o.uv = v.uv;
    return o;
}

// Light inputs. Every light the forward pass uses comes from here: one directional light and a
// hemisphere ambient, set per view by the renderer.
struct Lights {
    direction: vec3f,
    color: vec3f,
    sky: vec3f,
    ground: vec3f,
};

fn lights() -> Lights {
    var l: Lights;
    l.direction = normalize(view.light.xyz);
    l.color = view.light_color.rgb;
    l.sky = view.ambient_sky.rgb;
    l.ground = view.ambient_ground.rgb;
    return l;
}

fn ambient(l: Lights, dir: vec3f) -> vec3f {
    return mix(l.ground, l.sky, dir.y * 0.5 + 0.5);
}

// Surface of a fragment from the material constants of its instance. Texture layers apply
// here on front faces; a plain back face keeps the constants and faces the camera.
struct Surface {
    albedo: vec3f,
    alpha: f32,
    metallic: f32,
    roughness: f32,
    emissive: vec3f,
    normal: vec3f,
};

fn surface(f: VertexOut, front: bool) -> Surface {
    var s: Surface;
    s.albedo = f.color.rgb;
    s.alpha = f.color.a;
    s.metallic = clamp(f.material.x, 0.0, 1.0);
    s.roughness = clamp(f.material.y, MIN_ROUGHNESS, 1.0);
    s.emissive = f.emissive;
    s.normal = normalize(f.normal);
    if (!front) {
        s.normal = -s.normal;
    }
    return s;
}

fn distribution_ggx(ndh: f32, a: f32) -> f32 {
    let a2 = a * a;
    let d = ndh * ndh * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

fn visibility_smith(ndv: f32, ndl: f32, a: f32) -> f32 {
    let a2 = a * a;
    let gv = ndl * sqrt((ndv - a2 * ndv) * ndv + a2);
    let gl = ndv * sqrt((ndl - a2 * ndl) * ndl + a2);
    return 0.5 / max(gv + gl, 1e-7);
}

fn fresnel_schlick(cos_theta: f32, f0: vec3f) -> vec3f {
    let x = 1.0 - cos_theta;
    let x2 = x * x;
    return f0 + (vec3f(1.0) - f0) * (x2 * x2 * x);
}

// Analytic fit of the split sum environment BRDF (Karis), scale and bias of f0.
fn env_brdf(roughness: f32, ndv: f32) -> vec2f {
    let c0 = vec4f(-1.0, -0.0275, -0.572, 0.022);
    let c1 = vec4f(1.0, 0.0425, 1.04, -0.04);
    let r = roughness * c0 + c1;
    let a004 = min(r.x * r.x, exp2(-9.28 * ndv)) * r.x + r.y;
    return vec2f(-1.04, 1.04) * a004 + r.zw;
}

fn shade(s: Surface, v: vec3f, l: Lights) -> vec3f {
    let n = s.normal;
    let a = s.roughness * s.roughness;
    let f0 = mix(vec3f(0.04), s.albedo, s.metallic);
    let diffuse_color = s.albedo * (1.0 - s.metallic);
    let ndv = max(dot(n, v), 1e-4);
    let h = normalize(l.direction + v);
    let ndl = max(dot(n, l.direction), 0.0);
    let ndh = max(dot(n, h), 0.0);
    let ldh = max(dot(l.direction, h), 0.0);
    let f = fresnel_schlick(ldh, f0);
    let specular = distribution_ggx(ndh, a) * visibility_smith(ndv, ndl, a) * f;
    let diffuse = (vec3f(1.0) - f) * diffuse_color / PI;
    let direct = (diffuse + specular) * l.color * ndl;
    let ab = env_brdf(s.roughness, ndv);
    let r = reflect(-v, n);
    let indirect_specular = (f0 * ab.x + ab.y) * ambient(l, mix(r, n, a));
    let indirect_diffuse = diffuse_color * ambient(l, n);
    return direct + indirect_diffuse + indirect_specular + s.emissive;
}

fn output(color: vec3f, alpha: f32) -> vec4f {
    switch blend_mode {
        case 0u: {
            return vec4f(color, 1.0);
        }
        case 4u, 7u: {
            return vec4f(mix(vec3f(1.0), color, alpha), alpha);
        }
        case 5u, 8u: {
            return vec4f(color * alpha, alpha);
        }
        default: {
            return vec4f(color, alpha);
        }
    }
}

@fragment fn fs(f: VertexOut, @builtin(front_facing) front: bool) -> @location(0) vec4f {
    let s = surface(f, front);
    let v = normalize(view.camera.xyz - f.world);
    return output(shade(s, v, lights()), s.alpha);
}
