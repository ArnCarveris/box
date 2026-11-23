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
//
// The field frame moves with the WaterFlow of the Water of the field, while the water under it can
// flow differently (a bending brook made of lengths, each with its own WaterFlow). Once a frame the
// flow pass writes the velocity of every cell into the velocity texture (same addressing): the
// frame velocity plus the weighted average of the velocities of the flowing Water rectangles
// covering it relative to the frame, each weight fading out smoothly toward the edges of its
// rectangle, the frame velocity where none covers it. Before a step of a field where a rectangle
// flows relative to the frame, advect moves the heights (current and previous) and the foam
// semi-Lagrangian into a scratch texture, looking back along the velocity relative to the frame
// (at most REACH cells, bilinear), and settle copies them back; a cell whose relative velocity is
// zero copies its value unchanged.

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
    // Velocity of the frame X and Z (world units per second), seconds of a step per world unit of
    // a cell, cells inside the edges of a rectangle over which its weight fades.
    flow: vec4f,
    // First flowing Water rectangle of the field, its rectangles.
    flows: vec4u,
};

// Cells a step looks back along the velocity relative to the frame at most,
// BOX_RENDER_RIPPLE_FLOW_REACH.
const REACH: f32 = 2.0;

// The rectangle of a flowing Water (box_render_ripple_flow_t), in cells from the window corner:
// its center (a.xy), its first axis in world XZ (a.zw, the second is (-a.w, a.z)), its half size
// along them (b.xy) and its velocity in world units per second (b.zw).
struct FlowRect {
    a: vec4f,
    b: vec4f,
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

// The flow pass: the rectangles of the frame and the velocity texture it writes, the frame
// velocity plus the velocity relative to it (xy) and the velocity relative to it (zw).
@group(0) @binding(6) var<storage, read> rects: array<FlowRect>;
@group(0) @binding(7) var velocity_out: texture_storage_2d<rgba32float, write>;

// The advect and settle passes: the texture they read, the one they write and the velocities.
@group(0) @binding(8) var advect_src: texture_storage_2d<r32float, read>;
@group(0) @binding(9) var advect_dst: texture_storage_2d<r32float, write>;
@group(0) @binding(10) var velocity: texture_storage_2d<rgba32float, read>;

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

// Rise smoothly from 0 to 1 over fade, 1 for a fade of 0 or less.
fn flow_smooth(d: f32, fade: f32) -> f32 {
    if (fade <= 0.0) {
        return 1.0;
    }
    let t = clamp(d / fade, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// Weight of a rectangle at a point (box_render_ripples_flow_weight): 0 outside it, rising over
// fade inside each edge to 1.
fn flow_weight(rect: FlowRect, p: vec2f, fade: f32) -> f32 {
    let d = p - rect.a.xy;
    let axis = rect.a.zw;
    let l = vec2f(d.x * axis.x + d.y * axis.y, d.x * -axis.y + d.y * axis.x);
    let inside = rect.b.xy - abs(l);
    if (inside.x <= 0.0 || inside.y <= 0.0) {
        return 0.0;
    }
    return flow_smooth(inside.x, fade) * flow_smooth(inside.y, fade);
}

// Write the velocity of a cell (box_render_ripples_flow_at).
@compute @workgroup_size(8, 8)
fn flow(@builtin(global_invocation_id) id: vec3u) {
    let r = params.grid.x;
    if (id.x >= r || id.y >= r) {
        return;
    }
    let p = vec2f(cell_of(id.xy)) + 0.5;
    let frame = params.flow.xy;
    var weight = 0.0;
    var sum = vec2f(0.0);
    for (var i = 0u; i < params.flows.y; i++) {
        let rect = rects[params.flows.x + i];
        let w = flow_weight(rect, p, params.flow.w);
        if (w > 0.0) {
            weight += w;
            sum += w * (rect.b.zw - frame);
        }
    }
    var relative = vec2f(0.0);
    if (weight > 0.0) {
        relative = sum / weight;
    }
    textureStore(velocity_out, id.xy, vec4f(frame + relative, relative));
}

// Value of a cell of the window in advect_src, 0 outside the window.
fn advect_at(cell: vec2i) -> f32 {
    let r = i32(params.grid.x);
    if (any(cell < vec2i(0)) || any(cell >= vec2i(r))) {
        return 0.0;
    }
    let texel = (vec2u(cell) + params.grid.yz) % params.grid.x;
    return textureLoad(advect_src, texel).r;
}

// Advect a cell: the value of advect_src a step back along its velocity relative to the frame
// (bilinear), the value of the cell itself where that velocity is zero.
@compute @workgroup_size(8, 8)
fn advect(@builtin(global_invocation_id) id: vec3u) {
    let r = params.grid.x;
    if (id.x >= r || id.y >= r) {
        return;
    }
    var d = textureLoad(velocity, id.xy).zw * params.flow.z;
    if (d.x == 0.0 && d.y == 0.0) {
        textureStore(advect_dst, id.xy, textureLoad(advect_src, id.xy));
        return;
    }
    let reach = length(d);
    if (reach > REACH) {
        d *= REACH / reach;
    }
    // The center of the cell is cell + 0.5, the texel centers are too: q is in cells of the
    // lower left texel of the four around the point looked back to
    let q = vec2f(cell_of(id.xy)) - d;
    let base = floor(q);
    let t = q - base;
    let c = vec2i(base);
    let v00 = advect_at(c);
    let v10 = advect_at(c + vec2i(1, 0));
    let v01 = advect_at(c + vec2i(0, 1));
    let v11 = advect_at(c + vec2i(1, 1));
    let value = mix(mix(v00, v10, t.x), mix(v01, v11, t.x), t.y);
    textureStore(advect_dst, id.xy, vec4f(value, 0.0, 0.0, 0.0));
}

// Copy a cell of advect_src into advect_dst, the advected values back into the field.
@compute @workgroup_size(8, 8)
fn settle(@builtin(global_invocation_id) id: vec3u) {
    let r = params.grid.x;
    if (id.x >= r || id.y >= r) {
        return;
    }
    textureStore(advect_dst, id.xy, textureLoad(advect_src, id.xy));
}
