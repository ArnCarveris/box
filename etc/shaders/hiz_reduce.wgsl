@group(0) @binding(0) var src: texture_2d<f32>;
@group(0) @binding(1) var dst: texture_storage_2d<r32float, write>;

@compute @workgroup_size(HIZ_LEVEL_GROUP, HIZ_LEVEL_GROUP)
fn cs_main(@builtin(global_invocation_id) id: vec3<u32>) {
  let out_size = textureDimensions(dst);
  if (id.x >= out_size.x || id.y >= out_size.y) {
    return;
  }
  let size = vec2<i32>(textureDimensions(src, 0));
  let base = vec2<i32>(id.xy) * 2;
  let span = vec2<i32>(2, 2) + vec2(size.x & 1, size.y & 1);
  var result = 1.0;
  for (var y = 0; y < span.y; y = y + 1) {
    for (var x = 0; x < span.x; x = x + 1) {
      let c = clamp(base + vec2(x, y), vec2(0, 0), size - vec2(1, 1));
      result = min(result, textureLoad(src, c, 0).r);
    }
  }
  textureStore(dst, vec2<i32>(id.xy), vec4(result, 0.0, 0.0, 0.0));
}
