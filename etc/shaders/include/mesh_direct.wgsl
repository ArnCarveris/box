// Direct light of the mesh shaders: the GGX terms and the shadow terminator of a shading normal
// (shadow_terminator) both share, and the loop over the lights of a fragment, with the shadows
// of the spot and point lights (scene_punctual_shadows) and the cookie of the first directional
// light (scene_cookies). The includer includes mesh_common.wgsl first and declares the Surface of
// its fragments, with at least the albedo, the metallic, the occlusion, the shading normal, the
// geometric normal and shade, the share of the first directional light the heights of a terrain
// hold back (0 for none), and light_lobes, the light its surface reflects of one light, whose
// lobes of the shading normal fade by shadow_terminator.
#pragma once

// Light reflected toward the eye, split into its diffuse and specular parts; the emission is
// part of the specular one.
struct Lit {
    diffuse: vec3f,
    specular: vec3f,
};

// What a view of an impostor frame (render/impostor.c) shows, unlit and without the emission:
// view.lights.z 2 the albedo, 3 the world space normal as n * 0.5 + 0.5, 4 the depth of the
// fragment in the box of the frame, 0 at the near plane and 1 at the far plane of the
// orthographic camera.
fn impostor_channel(albedo: vec3f, normal: vec3f, world: vec3f) -> Lit {
    switch view.lights.z {
        case 3u: {
            return Lit(normalize(normal) * 0.5 + 0.5, vec3f(0.0));
        }
        case 4u: {
            let d = -(view.view * vec4f(world, 1.0)).z;
            let range = max(view.cluster.w - view.cluster.z, 1e-6);
            return Lit(vec3f(clamp((d - view.cluster.z) / range, 0.0, 1.0)), vec3f(0.0));
        }
        default: {
            return Lit(albedo, vec3f(0.0));
        }
    }
}

fn distribution_ggx(ndh: f32, a: f32) -> f32 {
    let a2 = a * a;
    let d = ndh * ndh * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

fn visibility_smith(ndv: f32, ndl: f32, a: f32) -> f32 {
    let a2 = a * a;
    let gv = ndl * sqrt((ndv - a2 * ndv) * ndv + a2);
    let gl = ndv * sqrt((ndl - a2 * ndl) * ndl + a2);
    return 0.5 / max(gv + gl, 1e-7);
}

fn fresnel_schlick(cos_theta: f32, f0: vec3f) -> vec3f {
    let x = 1.0 - cos_theta;
    let x2 = x * x;
    return f0 + (vec3f(1.0) - f0) * (x2 * x2 * x);
}

// Shadow terminator of a shading normal ns, such as a normal map, on a surface of geometric
// normal ng for a light toward l (Chiang, Li and Burley 2019, "Taming the Shadow Terminator").
// A shading normal that faces the light where the geometric surface turns away from it would
// light the side the surface shadows, up to a hard cut where the light is shadowed. The fraction
// G = dot(ng, l) / (dot(ns, l) * dot(ng, ns)) of the light that the bumps of the shading normal
// leave unshadowed fades it smoothly as -G^3 + G^2 + G: 0 with the light below the geometric
// surface, 1 where ns is ng or the light is high enough above it.
fn shadow_terminator(ng: vec3f, ns: vec3f, l: vec3f) -> f32 {
    let ngl = dot(ng, l);
    if (ngl <= 0.0) {
        return 0.0;
    }

    let d = dot(ns, l) * dot(ng, ns);
    if (d <= ngl) {
        return 1.0;
    }

    let g = ngl / d;
    return g * (1.0 + g * (1.0 - g));
}

// Whether a light toward l reaches a side of a surface of geometric normal ng: the front lobes
// of side 0 and the back lobe of side 1 fade by the shadow terminator, which is 0 with the light
// behind the geometric surface of the side, so its shadows and cookie need not be read.
fn light_reaches(ng: vec3f, l: vec3f, side: u32) -> bool {
    let g = dot(ng, l);
    return select(g > 0.0, g < 0.0, side == 1u);
}

// Direct light: the directional lights, then the lights of the cluster. With
// scene_planet_light the sun of the planet of the fragment (fragment_planet) is attenuated by
// the transmittance of its atmosphere at world, and its sky lights the diffuse term
// (planet_light_sky) as a Lambert surface, unshadowed but occluded. With sides 2 each light
// is evaluated again thickness behind the surface along the view ray, for the back lobe of a
// diffuse transmission, which light_lobes returns for side 1. In an area of a portal-room scene
// (areas.wgsl) a light of the clusters lights the fragment only from the area of the fragment,
// the lights through its portals follow the cluster lights, each only along rays through its
// openings, and the directional lights reach an area only through openings to the outdoors
// (area_sun). The directional, cluster and portal lights share one loop, and the sides one loop
// per light, so the shadows, the cookie and the lobes each have one call site: D3D compilers
// inline every call. relative is world relative to the render origin, which the shadows of the
// first directional light are looked up at.
fn lights_direct(s: Surface, v: vec3f, world: vec3f, relative: vec3f, cluster: u32,
    sides: u32, thickness: f32) -> Lit
{
    var sum = Lit(vec3f(0.0), vec3f(0.0));
    var sky = vec3f(0.0);
    var planet = PlanetSun(vec3f(0.0), vec3f(1.0), vec3f(0.0, 1.0, 0.0), vec3f(0.0));
    if (scene_planet_light) {
        planet = planet_light_sun(fragment_planet, world);
    }
    let directional = view.lights.x;
    var count = directional;
    var first = 0u;
    if (view.lights.y > view.lights.x) {
        first = 1u + 2u * cluster_count(view) + clusters[1u + 2u * cluster];
        count += cluster_lights(clusters[2u + 2u * cluster]);
    }

    let portal = count;
    count += area_through_count();

    for (var i = 0u; i < count; i++) {
        var light: Light;
        var through = 0u;
        if (i < directional) {
            light = lights[i];
        } else if (i < portal) {
            light = lights[clusters[first + i - directional]];
            if (!area_lights(light)) {
                continue;
            }
        } else {
            through = area_through_record(i - portal);
            let index = area_through_light(through);
            if (index < 0 || u32(index) >= view.lights.y) {
                continue;
            }
            light = lights[u32(index)];
        }

        for (var side = 0u; side < sides; side++) {
            var at = world;
            var at_relative = relative;
            var n = s.geometric;
            if (side == 1u) {
                at = world - v * thickness;
                at_relative = relative - v * thickness;
                n = -s.geometric;
            }

            var l: vec3f;
            var radiance = light.color;
            var visible = 1.0;
            if (i < directional) {
                l = normalize(light.position);
                if (scene_planet_light) {
                    if (side == 0u) {
                        sky += planet_light_sky(planet, l, s.normal) * light.color;
                    }
                    radiance *= planet_light_factor(planet, l);
                }
                if (!light_reaches(s.geometric, l, side)) {
                    continue;
                }
                if (i == 0u) {
                    if (side == 1u) {
                        visible = shadow_factor_back(at, at_relative, n) * (1.0 - s.shade);
                    } else {
                        visible = shadow_factor(at, at_relative, n) * (1.0 - s.shade);
                    }
                    if (scene_cookies) {
                        visible *= light_cookie(at, l, n);
                    }
                }
                radiance *= area_sun(at, l, visible);
                visible = 1.0;
            } else {
                let delta = light.position - at;
                let dist = length(delta);
                l = delta / max(dist, 1e-4);
                if (!light_reaches(s.geometric, l, side)) {
                    continue;
                }
                radiance *= attenuation(light, l, dist);
                if (scene_punctual_shadows && light.shadow < view.spot_config.x) {
                    visible = punctual_shadow(light, at, n);
                }
                if (i >= portal) {
                    radiance *= area_through(through, at, light.position);
                }
            }

            let lit = light_lobes(s, v, l, radiance, side);
            sum.diffuse += lit.diffuse * visible;
            sum.specular += lit.specular * visible;
        }
    }

    if (scene_planet_light) {
        sum.diffuse += s.albedo * (1.0 - s.metallic) * (s.occlusion / PI) * sky;
    }

    return sum;
}
