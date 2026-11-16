// Compose pass: places the slots of a mover page in the world. Each slot holds its transform
// relative to its mover and the record of its mover; the world transform is the transform of the
// mover times it. A frame record marked with the frame bit moves with the mover too.

struct InstanceTransform {
    rows: array<vec4f, 3>,
};

const MOVER_INDEX = 0x7FFFFFFFu;
const MOVER_FRAME = 0x80000000u;

@group(0) @binding(0) var<storage, read> movers: array<InstanceTransform>;
@group(0) @binding(1) var<storage, read> ids: array<u32>;
@group(0) @binding(2) var<storage, read> locals: array<InstanceTransform>;
@group(0) @binding(3) var<storage, read_write> worlds: array<InstanceTransform>;
@group(0) @binding(4) var<storage, read> frames: array<InstanceTransform>;
@group(0) @binding(5) var<storage, read_write> frame_worlds: array<InstanceTransform>;

fn compose(m: InstanceTransform, l: InstanceTransform) -> InstanceTransform {
    var out: InstanceTransform;
    for (var r = 0u; r < 3u; r++) {
        let a = m.rows[r];
        out.rows[r] = a.x * l.rows[0] + a.y * l.rows[1] + a.z * l.rows[2] +
            vec4f(0.0, 0.0, 0.0, a.w);
    }
    return out;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3u) {
    let slot = id.x;
    if (slot >= arrayLength(&ids) || slot >= arrayLength(&locals) ||
        slot >= arrayLength(&worlds)) {
        return;
    }

    let record = ids[slot];
    let mover = movers[min(record & MOVER_INDEX, arrayLength(&movers) - 1u)];
    worlds[slot] = compose(mover, locals[slot]);
    if (arrayLength(&frame_worlds) >= arrayLength(&ids) && slot < arrayLength(&frames)) {
        var frame = frames[slot];
        if ((record & MOVER_FRAME) != 0u) {
            frame = compose(mover, frame);
        }
        frame_worlds[slot] = frame;
    }
}
