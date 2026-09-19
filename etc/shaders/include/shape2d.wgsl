#pragma once

// Analytic 2D shapes. A rectangle instance carries four packed words: the
// corner radii, the edge widths, the border color and the two half extents of
// the rectangle. Radii and widths are ratios of the smaller half extent, so the
// fragment stage rebuilds the shape in a space where that half extent is one
// and the rounded corners stay round whatever the aspect of the rectangle is.
// Nothing here depends on the mesh, which is why changing a radius uploads an
// instance instead of building geometry.

// Bit of the instance mode word that says the shape words are filled, which is
// BOX_MODE_SHAPE_SHIFT on the engine side.
const SHAPE2D_BIT: u32 = 0x8000u;

// Bit of the instance mode word that says the shape is a soft fill instead of a
// rectangle with a border, which is BOX_MODE_SOFT_SHIFT on the engine side. The
// edge widths are then the distance the fill fades out over.
const SHAPE2D_SOFT: u32 = 0x10u;

// Half extents of the rectangle in units of its smaller half extent.
fn shape2d_extent(packed: u32) -> vec2<f32> {
  let a = unpack2x16unorm(packed);
  let m = max(min(a.x, a.y), 1e-4);
  return a / m;
}

// Signed distance to a rounded rectangle centered on the origin, negative
// inside it. The radii are top left, top right, bottom right and bottom left.
fn shape2d_distance(p: vec2<f32>, he: vec2<f32>, radii: vec4<f32>) -> f32 {
  let pair = select(vec2(radii.w, radii.z), radii.xy, p.y >= 0.0);
  let r = min(select(pair.x, pair.y, p.x >= 0.0), min(he.x, he.y));
  let q = abs(p) - he + vec2(r);
  return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - r;
}

// Coverage of an outline at a distance, softened over one pixel footprint.
fn shape2d_coverage(d: f32, soft: f32) -> f32 {
  return clamp(0.5 - d / max(soft, 1e-6), 0.0, 1.0);
}

// Coverage of the shape and of its border ring at a point, the point being the
// normalized local position of the fragment inside the bounds of its mesh.
fn shape2d_eval(words: vec4<u32>, local: vec2<f32>, footprint: vec2<f32>)
    -> vec2<f32> {
  let he = shape2d_extent(words.w);
  let p = (local * 2.0 - vec2(1.0)) * he;
  let soft = max(length(footprint * 2.0 * he), 1e-6);
  let radii = unpack4x8unorm(words.x);
  let w = unpack4x8unorm(words.y);
  let cov = shape2d_coverage(shape2d_distance(p, he, radii), soft);
  let center = vec2(w.w - w.y, w.z - w.x) * 0.5;
  let inner_he = max(he - vec2(w.y + w.w, w.x + w.z) * 0.5, vec2(0.0));
  let inner_r = max(radii - vec4(
    max(w.x, w.w), max(w.x, w.y), max(w.z, w.y), max(w.z, w.w)), vec4(0.0));
  let inner = shape2d_distance(p - center, inner_he, inner_r);
  var ring = 0.0;
  if (max(max(w.x, w.y), max(w.z, w.w)) > 0.0) {
    ring = cov * (1.0 - shape2d_coverage(inner, soft));
  }

  return vec2(cov, ring);
}

// Coverage of a soft filled rounded rectangle, which is the shape the edge
// widths inset, faded out over twice those widths. A box shadow is that fill:
// the instance covers the shape plus its blur on every side, the widths are the
// blur, and the fade runs from fully covered at the outline of the inset shape
// to nothing at the edge of the instance. The ramp is smoothed once, which is
// close enough to the falloff of a blur and costs two multiplies.
fn shape2d_soft(words: vec4<u32>, local: vec2<f32>, footprint: vec2<f32>) -> f32 {
  let he = shape2d_extent(words.w);
  let p = (local * 2.0 - vec2(1.0)) * he;
  let px = max(length(footprint * 2.0 * he), 1e-6);
  let radii = unpack4x8unorm(words.x);
  let w = unpack4x8unorm(words.y);
  let inner_he = max(he - vec2(w.y + w.w, w.x + w.z) * 0.5, vec2(0.0));
  let inner_r = max(radii - vec4(
    max(w.x, w.w), max(w.x, w.y), max(w.z, w.y), max(w.z, w.w)), vec4(0.0));
  let center = vec2(w.w - w.y, w.z - w.x) * 0.5;
  let d = shape2d_distance(p - center, inner_he, inner_r);
  let t = shape2d_coverage(d, max(w.x + w.z, px));
  return t * t * (3.0 - 2.0 * t);
}
