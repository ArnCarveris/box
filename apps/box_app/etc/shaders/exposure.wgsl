#include "include/fx_params.wgsl"

@group(0) @binding(0) var<uniform> fx: Effect;
@group(0) @binding(1) var src: texture_2d<f32>;
@group(0) @binding(2) var<storage, read_write> histogram: array<atomic<u32>, EXPOSURE_BINS>;
@group(0) @binding(3) var<storage, read_write> state: Exposure;

var<workgroup> bins: array<atomic<u32>, EXPOSURE_BINS>;

// One workgroup covers a EXPOSURE_GROUP square of texels and holds a histogram
// of its own, so the global one is touched once per bin per workgroup.
@compute @workgroup_size(EXPOSURE_GROUP, EXPOSURE_GROUP)
fn cs_histogram(@builtin(global_invocation_id) gid: vec3<u32>,
                @builtin(local_invocation_index) index: u32) {
  atomicStore(&bins[index], 0u);
  workgroupBarrier();

  let size = textureDimensions(src, 0);
  if (gid.x < size.x && gid.y < size.y) {
    let texel = textureLoad(src, vec2<i32>(gid.xy), 0).rgb;
    let luma = fx_luma(fx_decode(texel, fx.frame.z));
    atomicAdd(&bins[fx_exposure_bin(luma)], 1u);
  }

  workgroupBarrier();
  let local = atomicLoad(&bins[index]);
  if (local != 0u) {
    atomicAdd(&histogram[index], local);
  }
}

// Weighted average of the histogram and the exposure it adapts to, which the
// next frame reads as its starting point. The histogram is cleared here so the
// frame that fills it does not need a pass of its own.
@compute @workgroup_size(1)
fn cs_average() {
  let speed_up = fx.params[0].x;
  let speed_down = fx.params[0].y;
  let min_ev = fx.params[0].z;
  let max_ev = fx.params[0].w;
  var aim = fx.params[1].x;
  let compensation = fx.params[1].y;
  let pinned = fx.params[1].z;
  if (aim <= 0.0) {
    aim = 0.18;
  }

  var weighted = 0.0;
  var counted = 0.0;
  for (var i = 1u; i < u32(EXPOSURE_BINS); i = i + 1u) {
    let count = f32(atomicLoad(&histogram[i]));
    weighted = weighted + count * f32(i);
    counted = counted + count;
  }

  for (var i = 0u; i < u32(EXPOSURE_BINS); i = i + 1u) {
    atomicStore(&histogram[i], 0u);
  }

  var average = 0.0;
  if (counted > 0.0) {
    average = fx_exposure_luma(weighted / counted);
  }

  let measured = clamp(log2(aim / max(average, 0.00001)), min_ev, max_ev);
  let current = clamp(state.ev, min_ev, max_ev);
  var next = current;
  if (pinned <= 0.5) {
    let step_up = max(speed_up, 0.0) * fx.frame.x;
    let step_down = max(speed_down, 0.0) * fx.frame.x;
    next = current + clamp(measured - current, -step_down, step_up);
  }

  state.ev = next;
  state.scale = exp2(next + compensation);
  state.average = average;
}
