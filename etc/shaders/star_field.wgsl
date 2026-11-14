// Star fields: one instanced draw of a quad per star of a StarField a view draws, after its
// opaque meshes and its sky, where the depth is still the clear value: at infinite distance,
// behind all geometry. Star i is generated from a hash of i and the seed, as box_render_star_sample
// (src/render/stars.c) generates it, and adds its light to the target.
//
// A star is a disc of radius size * sqrt(r) pixels with a ramp of one pixel at its edge; a star
// whose radius is below half a pixel is a point: its energy is spread with a tent filter over the
// at most two by two pixels nearest its center, so it is dimmer, not larger, and keeps its energy
// wherever it falls between pixels.

const PI: f32 = 3.14159265358979;

// Odd constant the hash keys of a star and of its values are spread by.
const GOLDEN: u32 = 0x9e3779b9u;

// Radius in pixels below which a star is drawn as a point, BOX_RENDER_STAR_MIN_RADIUS.
const MIN_RADIUS: f32 = 0.5;

struct StarDraw {
    // Clip space of the view from the local space of the field, applied to directions (w 0)
    clip_from_local: mat4x4f,
    // Width and height of the target in pixels, then their inverses
    viewport: vec4f,
    // brightness, falloff, size, saturation
    params: vec4f,
    // temperature_min, temperature_max, band, band_width in radians
    shape: vec4f,
    seed: u32,
    count: u32,
    pad0: u32,
    pad1: u32,
};

@group(1) @binding(0) var<uniform> draw: StarDraw;

struct VsOut {
    @builtin(position) position: vec4f,
    // Offset of the fragment from the center of the star in pixels
    @location(0) offset: vec2f,
    // Radius of the disc of the star in pixels, 0 for a point
    @location(1) @interpolate(flat) radius: f32,
    // Linear color times the emission at the center of a disc or on the pixel of a point
    @location(2) @interpolate(flat) color: vec3f,
};

fn star_hash(v: u32) -> u32 {
    var x = v;
    x = x ^ (x >> 16u);
    x = x * 0x7feb352du;
    x = x ^ (x >> 15u);
    x = x * 0x846ca68bu;
    x = x ^ (x >> 16u);
    return x;
}

// A value of a star uniform in (0, 1].
fn star_unit(key: u32, k: u32) -> f32 {
    let h = star_hash(key + k * GOLDEN);
    return (f32(h >> 8u) + 1.0) / 16777216.0;
}

// Display encoded blackbody color of a temperature, the brightest channel 255: the fit of
// Tanner Helland the blackbody() of the planet scene uses.
fn blackbody(kelvin: f32) -> vec3f {
    let t = clamp(kelvin, 1000.0, 40000.0) / 100.0;
    let r = select(329.698727446 * pow(max(t - 60.0, 1.0), -0.1332047592), 255.0, t <= 66.0);
    let g = select(288.1221695283 * pow(max(t - 60.0, 1.0), -0.0755148492),
        99.4708025861 * log(t) - 161.1195681661, t <= 66.0);
    let b_lo = select(138.5177312231 * log(max(t - 10.0, 1.0)) - 305.0447927307, 0.0, t <= 19.0);
    let b = select(b_lo, 255.0, t >= 66.0);
    return clamp(vec3f(r, g, b), vec3f(0.0), vec3f(255.0));
}

fn linear_from_srgb(c: vec3f) -> vec3f {
    let lo = c / 12.92;
    let hi = pow((c + 0.055) / 1.055, vec3f(2.4));
    return select(hi, lo, c <= vec3f(0.04045));
}

@vertex fn vs(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> VsOut {
    var out: VsOut;
    out.position = vec4f(0.0, 0.0, -1.0, 1.0);
    out.offset = vec2f(0.0);
    out.radius = 0.0;
    out.color = vec3f(0.0);

    let key = star_hash(ii ^ star_hash(draw.seed + GOLDEN));
    let u0 = star_unit(key, 0u);
    let u1 = star_unit(key, 1u);
    let u2 = star_unit(key, 2u);
    let u3 = star_unit(key, 3u);
    let u4 = star_unit(key, 4u);
    let u5 = star_unit(key, 5u);

    let phi = 2.0 * PI * u1;
    var y = 2.0 * u2 - 1.0;
    if (u0 <= draw.shape.z) {
        let g = sqrt(-2.0 * log(u2)) * cos(2.0 * PI * u3);
        y = sin(clamp(g * draw.shape.w, -0.5 * PI, 0.5 * PI));
    }

    let rxz = sqrt(max(1.0 - y * y, 0.0));
    let local = vec3f(rxz * cos(phi), y, rxz * sin(phi));
    let clip = draw.clip_from_local * vec4f(local, 0.0);
    if (clip.w <= 1e-6) {
        return out;
    }

    let relative = select(1.0, pow(u4, draw.params.y), draw.params.y > 0.0);
    let temperature = draw.shape.x + (draw.shape.y - draw.shape.x) * u5;
    let radius = draw.params.z * sqrt(relative);
    let point = radius < MIN_RADIUS;
    let drawn = select(radius, 0.0, point);
    let peak = select(draw.params.x * radius * radius / (radius * radius + 1.0 / 12.0),
        draw.params.x * PI * radius * radius, point);
    let bb = linear_from_srgb(blackbody(temperature) / 255.0);
    let color = vec3f(1.0) + (bb - vec3f(1.0)) * draw.params.w;
    if (peak <= 0.0) {
        return out;
    }

    // Corners of two triangles of a quad that covers the disc and its ramp, or the tent
    var corners = array<vec2f, 6>(
        vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0),
        vec2f(-1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));
    let extent = drawn + 1.0;
    let offset = corners[vi % 6u] * extent;
    let ndc = clip.xy / clip.w + offset * 2.0 * draw.viewport.zw;
    out.position = vec4f(ndc, 0.0, 1.0);
    out.offset = offset;
    out.radius = drawn;
    out.color = color * peak;
    return out;
}

@fragment fn fs(v: VsOut) -> @location(0) vec4f {
    var coverage = clamp(v.radius + 0.5 - length(v.offset), 0.0, 1.0);
    if (v.radius <= 0.0) {
        let tent = max(vec2f(1.0) - abs(v.offset), vec2f(0.0));
        coverage = tent.x * tent.y;
    }

    if (coverage <= 0.0) {
        discard;
    }

    return vec4f(v.color * coverage, 0.0);
}
