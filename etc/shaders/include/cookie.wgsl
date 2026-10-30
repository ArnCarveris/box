// Light cookie of the first directional light of the scene (LightCookie), read by the forward
// pass and the particle draw: a tileable single channel texture that scales the direct light of
// that light, diffuse and specular, together with its shadow.
//
// The plane projection moves a world position p along the direction l toward the light onto the
// plane y = height, q = p + l * (height - p.y) / l.y, and reads (q.xz + offset) / size; positions
// at or above the plane and a light at or below the horizon are not affected. The light
// projection reads the position along the local X and Y axes of the light plus offset, over
// size. The offset already holds the velocity times the time, wrapped to the size on the CPU.
//
// The texture is read as stored through a repeat sampler; the level follows the world size of
// the pixel at the position over the world size of a texel, as textureSampleLevel needs no
// derivatives and the plane projection skips positions. With t the value read the cookie is
// mix(1, saturate((t - threshold) / (2 * softness) + 0.5), strength). A view without a cookie
// (cookie_config.x 0) binds a white texture and skips the lookup in uniform control flow.
#pragma once

@group(0) @binding(17) var cookie_texture: texture_2d<f32>;
@group(0) @binding(18) var cookie_sampler: sampler;

// Smallest ramp of the remap, which makes a softness of 0 a step at the threshold.
const COOKIE_MIN_RAMP: f32 = 1e-5;

// Cookie of the first directional light at a world position, with l the normalized direction
// toward that light: 1 where it does not apply.
fn light_cookie(world: vec3f, l: vec3f) -> f32 {
    if (view.cookie_config.x == 0u) {
        return 1.0;
    }

    var plane: vec2f;
    if (view.cookie_config.y == 0u) {
        let height = view.cookie.y;
        if (l.y <= 0.0 || world.y >= height) {
            return 1.0;
        }

        let q = world + l * ((height - world.y) / l.y);
        plane = q.xz;
    } else {
        plane = vec2f(dot(world, view.cookie_x.xyz), dot(world, view.cookie_y.xyz));
    }

    let uv = (plane + view.cookie_offset.xy) * view.cookie.x;
    let texels = vec2f(textureDimensions(cookie_texture));
    var pixel = 2.0 * view.cluster.y * view.viewport.w;
    if (view.lights.w == 0u) {
        pixel *= length(world - view.camera.xyz);
    }

    let lod = log2(max(pixel * view.cookie.x * max(texels.x, texels.y), 1e-6));
    let t = textureSampleLevel(cookie_texture, cookie_sampler, uv, max(lod, 0.0)).r;
    let ramp = max(2.0 * view.cookie.w, COOKIE_MIN_RAMP);
    let lit = saturate((t - view.cookie.z) / ramp + 0.5);
    return mix(1.0, lit, view.cookie_offset.z);
}
