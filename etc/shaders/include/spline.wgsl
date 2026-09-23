struct SweepResult {
  position: vec3<f32>,
  normal: vec3<f32>,
};

fn spline_sweep(position: vec3<f32>, normal: vec3<f32>, center: vec3<f32>,
    tangent: vec3<f32>, profile: vec4<f32>) -> SweepResult {
  var result: SweepResult;
  result.position = position;
  result.normal = normal;
  if (dot(tangent, tangent) > 1e-12) {
    let direction = normalize(tangent);
    var right = cross(vec3(0.0, 1.0, 0.0), direction);
    if (dot(right, right) < 1e-12) {
      right = cross(vec3(0.0, 0.0, 1.0), direction);
    }
    right = normalize(right);
    let up = normalize(cross(direction, right));
    result.position = center + right * profile.x + up * profile.y;
    result.normal = normalize(right * profile.z + up * profile.w);
  }
  return result;
}
