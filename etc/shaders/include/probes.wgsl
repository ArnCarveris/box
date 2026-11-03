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

// Every probe is SH_COEFFICIENTS rgb irradiance coefficients E_lm (Ramamoorthi and Hanrahan),
// in Sloan's real SH basis; cell_valid has a byte per cell of the grid, four to a word, whose
// bit x + 2y + 4z is set when that corner probe of the cell is valid: clear inside static
// geometry. A cell is a probe with a neighbour after it on every axis with more than one probe.
// probe_visibility holds an octahedral map per probe of VISIBILITY_SIZE texels on each side,
// each the mean and mean square distance to the surfaces its faces saw in probe spacings, packed
// as two halves (bake_sh.wgsl).
@group(3) @binding(0) var<uniform> volume: Volume;
@group(3) @binding(1) var<storage, read> probes: array<vec4f>;
@group(3) @binding(2) var<storage, read> cell_valid: array<u32>;
@group(3) @binding(3) var<storage, read> probe_visibility: array<u32>;

const SH_COEFFICIENTS: u32 = 9u;
