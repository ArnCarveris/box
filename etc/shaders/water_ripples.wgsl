// Fields of WaterRipples (gpu_ripples.c): a height field per Water, stepped by the damped wave
// equation h' = h + keep (h - h_prev) + C^2 laplacian(h) at a fixed step. A step reads the latest
// heights from current and the ones before from previous, which it overwrites with the new
// heights; the backend then swaps the two. Cells are addressed modulo the resolution: the texel
// of cell l of the window (0..resolution - 1 from its corner) is (corner texel + l) mod
// resolution, so a window that moves by whole cells keeps the heights of the cells it still
// covers, and clear only flattens the cells that entered it. Outside the window the water is
// flat, and a band of resolution / 16 cells along the border damps the heights toward 0 so waves
// do not reflect off it.
//
// Next to the heights each field has a foam channel, 0..1 per cell, in a second pair of textures
// stepped the same way: a step reads the foam from foam_current and writes it to foam_next. The
// foam takes a share of the difference to its neighbours (spread), keeps a factor per step
// (decay), forms where the slope of the heights is steeper than CREST_SLOPE (crest foam) and
// grows under the sources.

// The step uniform of a dispatch (box_gpu_ripple_step_t).
struct Step {
    // Resolution, texel of the window corner X and Z, sources of the field.
    grid: vec4u,
    // Cells the window moved X and Z since the previous frame, 1 on the first step of the frame,
    // first source of the field.
    shift: vec4i,
    // (c dt / dx)^2, velocity kept per step, border cells, time fraction of the frame at the end
    // of the step.
    wave: vec4f,
    // Share of the difference to its neighbours the foam takes per step, factor of the foam kept
    // per step, foam formed per step per unit of slope above CREST_SLOPE, 1 / (2 world units of
    // a cell).
    foam: vec4f,
};

// Slope of the heights (world units per world unit) above which crest foam forms,
// BOX_RENDER_RIPPLE_CREST_SLOPE.
const CREST_SLOPE: f32 = 0.1;

// A source of a field (box_render_ripple_source_t), in cells from the window corner: its center
// at the start (a.xy) and the end (a.zw) of the frame, its half size along its own X and Z (b.xy)
// and its X axis in world XZ (b.zw), the height it adds at its center per step (c.x) and 1 in
// bitcast<u32>(c.y) for a ripple_at or foam_at disturbance applied on the first step only, and
// the foam it adds at its center per step (c.z).
struct RippleSource {
    a: vec4f,
    b: vec4f,
    c: vec4f,
};

@group(0) @binding(0) var current: texture_storage_2d<r32float, read>;
@group(0) @binding(1) var previous: texture_storage_2d<r32float, read_write>;
@group(0) @binding(2) var<uniform> params: Step;
@group(0) @binding(3) var<storage, read> sources: array<RippleSource>;
@group(0) @binding(4) var foam_current: texture_storage_2d<r32float, read>;
@group(0) @binding(5) var foam_next: texture_storage_2d<r32float, write>;

// Cell of the window of a texel.
fn cell_of(texel: vec2u) -> vec2i {
    let r = params.grid.x;
    return vec2i((texel + vec2u(r) - params.grid.yz) % r);
}

// Height of a cell of the window in current, 0 outside the window.
fn height_at(cell: vec2i) -> f32 {
    let r = i32(params.grid.x);
    if (any(cell < vec2i(0)) || any(cell >= vec2i(r))) {
        return 0.0;
    }
    let texel = (vec2u(cell) + params.grid.yz) % params.grid.x;
    return textureLoad(current, texel).r;
}

// Foam of a cell of the window in foam_current, 0 outside the window.
fn foam_at(cell: vec2i) -> f32 {
    let r = i32(params.grid.x);
    if (any(cell < vec2i(0)) || any(cell >= vec2i(r))) {
        return 0.0;
    }
    let texel = (vec2u(cell) + params.grid.yz) % params.grid.x;
    return textureLoad(foam_current, texel).r;
}

// Flatten the cells of previous that entered the window, and clear their foam in foam_next:
// those whose cell in the window before the move lies outside it.
@compute @workgroup_size(8, 8)
fn clear(@builtin(global_invocation_id) id: vec3u) {
    let r = params.grid.x;
    if (id.x >= r || id.y >= r) {
        return;
    }
    let before = cell_of(id.xy) + params.shift.xy;
    if (any(before < vec2i(0)) || any(before >= vec2i(i32(r)))) {
        textureStore(previous, id.xy, vec4f(0.0));
        textureStore(foam_next, id.xy, vec4f(0.0));
    }
}

// Step a cell: the wave equation, the foam, the sources over it and the damping of the border
// band.
@compute @workgroup_size(8, 8)
fn advance(@builtin(global_invocation_id) id: vec3u) {
    let r = params.grid.x;
    if (id.x >= r || id.y >= r) {
        return;
    }
    let cell = cell_of(id.xy);
    let h = textureLoad(current, id.xy).r;
    let before = textureLoad(previous, id.xy).r;
    let east = height_at(cell + vec2i(1, 0));
    let west = height_at(cell - vec2i(1, 0));
    let north = height_at(cell + vec2i(0, 1));
    let south = height_at(cell - vec2i(0, 1));
    let laplacian = east + west + north + south - 4.0 * h;
    var next = h + (h - before) * params.wave.y + params.wave.x * laplacian;

    let fc = textureLoad(foam_current, id.xy).r;
    let spread = foam_at(cell + vec2i(1, 0)) + foam_at(cell - vec2i(1, 0)) +
        foam_at(cell + vec2i(0, 1)) + foam_at(cell - vec2i(0, 1)) - 4.0 * fc;
    let slope = length(vec2f(east - west, north - south)) * params.foam.w;
    var foam = (fc + params.foam.x * spread) * params.foam.y +
        params.foam.z * max(slope - CREST_SLOPE, 0.0);

    let p = vec2f(cell) + 0.5;
    let first = u32(max(params.shift.w, 0));
    for (var i = 0u; i < params.grid.w; i++) {
        let s = sources[first + i];
        if (bitcast<u32>(s.c.y) != 0u && params.shift.z == 0) {
            continue;
        }
        let d = p - mix(s.a.xy, s.a.zw, params.wave.w);
        let axis = s.b.zw;
        let e = vec2f(dot(d, axis), dot(d, vec2f(-axis.y, axis.x))) / max(s.b.xy, vec2f(1e-3));
        let q = dot(e, e);
        if (q < 1.0) {
            let w = 1.0 - q;
            next += s.c.x * w * w;
            foam += s.c.z * w * w;
        }
    }

    let edge = f32(min(min(cell.x, cell.y), min(i32(r) - 1 - cell.x, i32(r) - 1 - cell.y)));
    let border = max(params.wave.z, 1.0);
    if (edge < border) {
        let k = 1.0 - edge / border;
        next *= 1.0 - 0.2 * k * k;
        foam *= 1.0 - 0.2 * k * k;
    }

    textureStore(previous, id.xy, vec4f(next, 0.0, 0.0, 0.0));
    textureStore(foam_next, id.xy, vec4f(saturate(foam), 0.0, 0.0, 0.0));
}
