// Irradiance probe volume a view samples (group 3), shared by the forward pass and the probe
// debug view (debug_probes.wgsl).
#pragma once
#include "sh.wgsl"

// Grid of the probe volume: probe (x, y, z) sits at origin.xyz + (x, y, z) * origin.w, x
// fastest in the probe buffer. size.w is 0 for the 1x1x1 volume of views that keep the
// hemisphere ambient, 1 when the view samples the probes.
struct Volume {
    origin: vec4f,
    size: vec4u,
};

// Every probe is the ZH3 of the irradiance (Ramamoorthi and Hanrahan) in Sloan's real SH
// basis, VOLUME_PROBE_VECTORS vec4 as zh3_load() of sh.wgsl reads them; cell_valid has a byte
// per cell of the grid, four to a word, whose bit x + 2y + 4z is set when that corner probe
// of the cell is valid: clear inside static geometry. A cell is a probe with a neighbour after
// it on every axis with more than one probe.
// probe_visibility holds an octahedral map per probe of VISIBILITY_SIZE texels on each side,
// each the mean and mean square distance to the surfaces its faces saw in probe spacings, packed
// as two halves (bake_sh.wgsl).
@group(3) @binding(0) var<uniform> volume: Volume;
@group(3) @binding(1) var<storage, read> probes: array<vec4f>;
@group(3) @binding(2) var<storage, read> cell_valid: array<u32>;
@group(3) @binding(3) var<storage, read> probe_visibility: array<u32>;

const SH_COEFFICIENTS: u32 = 9u;

// vec4 of a probe of the volume, BOX_GPU_VOLUME_PROBE_SIZE bytes
const VOLUME_PROBE_VECTORS: u32 = 4u;

// The ZH3 of a probe of the volume.
fn volume_probe(index: u32) -> Zh3 {
    let at = index * VOLUME_PROBE_VECTORS;
    return zh3_load(probes[at], probes[at + 1u], probes[at + 2u], probes[at + 3u]);
}
