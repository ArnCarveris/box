// Panel of the frame rate overlay. Every part of it is a rectangle in the
// pixels of the view, so one instanced draw covers the background, the two
// reference lines, the bars of the last second and the seven segment digits.
// The pass runs on the image of the view after its effect chain, which is
// display encoded unless the hardware encodes for the target itself.

struct OverlayRect {
  rect: vec4<f32>,
  color: vec4<f32>,
};

struct Overlay {
  size: vec4<f32>,
  flags: vec4<f32>,
};

@group(0) @binding(0) var<uniform> overlay: Overlay;
@group(0) @binding(1) var<storage, read> overlay_rects: array<OverlayRect>;

struct OverlayOut {
  @builtin(position) pos: vec4<f32>,
  @location(0) color: vec4<f32>,
};

@vertex fn vs_main(
  @builtin(vertex_index) vertex: u32,
  @builtin(instance_index) instance: u32
) -> OverlayOut {
  var order = array<u32, 6>(0u, 1u, 2u, 2u, 1u, 3u);
  let index = order[vertex];
  let corner = vec2(f32(index & 1u), f32((index >> 1u) & 1u));
  let entry = overlay_rects[instance];
  let pixel = entry.rect.xy + corner * entry.rect.zw;
  var out: OverlayOut;
  out.pos = vec4(pixel.x * overlay.size.z * 2.0 - 1.0,
    1.0 - pixel.y * overlay.size.w * 2.0, 0.0, 1.0);
  out.color = entry.color;
  return out;
}

@fragment fn fs_main(in: OverlayOut) -> @location(0) vec4<f32> {
  var rgb = max(in.color.rgb, vec3(0.0));
  if (overlay.flags.x > 0.5) {
    rgb = pow(rgb, vec3(2.2));
  }

  return vec4(rgb, in.color.a);
}
