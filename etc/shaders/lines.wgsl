// Debug line list. Every vertex is an end of a world space segment with a
// display encoded color, and the pass projects it with the matrices of the view
// it draws over. The color is converted to the space of the target the same way
// the mesh pass encodes its own: a float or an sRGB target wants linear light,
// an eight bit target the hardware does not encode for wants the value as it is.

#include "include/view.wgsl"

@group(0) @binding(0) var<uniform> u: View;

struct LineOut {
  @builtin(position) pos: vec4<f32>,
  @location(0) color: vec4<f32>,
};

@vertex fn vs_main(
  @location(0) position: vec3<f32>,
  @location(1) color: vec4<f32>
) -> LineOut {
  var out: LineOut;
  out.pos = u.proj * u.view * vec4(position, 1.0);
  out.color = color;
  return out;
}

@fragment fn fs_main(in: LineOut) -> @location(0) vec4<f32> {
  var rgb = max(in.color.rgb, vec3(0.0));
  if (u.gamma_encode < 0.5) {
    rgb = pow(rgb, vec3(2.2));
  }

  return vec4(rgb, in.color.a);
}
