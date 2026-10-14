// Scatter pass: copies the changed instances of a change buffer segment into their slots.
// A segment starts with the instance count and the words per instance, padded to four words,
// followed by one entry per instance: its slot and its words.

@group(0) @binding(0) var<storage, read> changes: array<u32>;
@group(0) @binding(1) var<storage, read_write> instances: array<u32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3u) {
    let count = changes[0];
    let words = changes[1];
    if (id.x >= count) {
        return;
    }

    let entry = 4u + id.x * (words + 1u);
    let slot = changes[entry] * words;
    for (var w = 0u; w < words; w++) {
        instances[slot + w] = changes[entry + 1u + w];
    }
}
