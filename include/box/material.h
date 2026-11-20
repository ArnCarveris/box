/**
 * @file box/material.h
 * @brief Material components: color, opacity, tint, PBR parameters, clear coat, anisotropy, sheen,
 * diffuse transmission, emission, Fresnel rim, transmission, distortion, water, water ripples and
 * wind sway.
 *
 * The box.material module resolves tints through the hierarchy and registers
 * the material layers a scene applies on top of the sampled surface.
 */

#ifndef BOX_MATERIAL_H
#define BOX_MATERIAL_H

#include "types.h"

#ifdef BOX_MATERIAL_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Slot of the material table a shape renders with, set by the engine and read only for
 * applications, inherited by instances.
 *
 * The material components, such as Rgb, PbrMaterial, Transmission, Water, TextureScroll and
 * Fresnel, bring it along. A prefab that owns one gets a slot its instances share, a 3D shape
 * that owns one a slot of its own.
 *
 * Slot 0 is the default material, of a shape without any material component. An owned id that
 * was never handed a slot (a light, a 2D shape, a text) is UINT32_MAX.
 */
ECS_STRUCT(MaterialId, {
    uint32_t value; /**< Slot of the material table */
});

/** Base color, display encoded bytes, inherited by instances.
 *
 * An instance of a prefab reads the color of the prefab until it sets its own, and a change of
 * the color of the prefab reaches every instance that does not set its own.
 */
ECS_STRUCT(Rgb, {
    uint8_t r; /**< Red */
    uint8_t g; /**< Green */
    uint8_t b; /**< Blue */
});

/** Opacity from 0 (invisible) to 1 (opaque), default 1.
 *
 * Outside a UI tree it multiplies into the alpha of every shape below the entity, which needs
 * no shape of its own, so one Opacity on a node fades a hierarchy. In a UI tree an element with
 * children composites its subtree with the opacity instead.
 *
 * A shape whose resulting alpha is under 1 is drawn by the sorted blended pass and casts no
 * shadow; at 1 it stays in its opaque batch. A shape with AlphaCutoff tests the alpha against
 * its cutoff in the opaque pass instead. A shape with Transmission or Water ignores it.
 */
ECS_STRUCT(Opacity, {
    float value; /**< Opacity */
});

/** Cutoff a constructed AlphaCutoff tests against */
#define BOX_MATERIAL_CUTOFF (0.5f)

/** Alpha test of a shape, default 0.5: its fragments are kept or discarded, not blended.
 * Inherited by instances, like Rgb. Only 3D meshes are alpha tested, 2D shapes and text ignore
 * it; picking does not read textures, so a shape is picked by its whole geometry, holes included.
 *
 * A fragment is tested with the alpha the shape draws with without it (its Opacity times that of
 * its ancestors) times the alpha of its albedo texture (PbrTextures). Below value, clamped to
 * 0..1, it is discarded; otherwise it is written as opaque.
 *
 * With an Opaque or missing BlendMode it draws in the opaque pass whatever its Opacity, with its
 * holes in depth, shadows, captures and BackFacePlain back faces; otherwise it discards in the
 * sorted blended pass and casts no shadow. A shape with Transmission or Water ignores it.
 */
ECS_STRUCT(AlphaCutoff, {
    float value; /**< Alpha below which a fragment is discarded */
});

/** Color the display encoded base color (Rgb) mixes toward by the strength in a, before lighting.
 *
 * It is not transparency (Opacity) and applies to every shape below it through ChildOf and
 * Parent, composed from the root down: a child's tint mixes the result of its ancestors'. An
 * albedo texture multiplies the tinted color; emission is not tinted.
 *
 * Light and reflection bakes ignore it: they capture the color without the tint, and changing a
 * tint rebakes nothing.
 */
ECS_STRUCT(Tint, {
    uint8_t r;  /**< Red of the tint color */
    uint8_t g;  /**< Green of the tint color */
    uint8_t b;  /**< Blue of the tint color */
    uint8_t a;  /**< Tint strength, 0 leaves the color and 255 replaces it */
});

/** Metallic of a surface without PbrMaterial and of a constructed PbrMaterial: a dielectric */
#define BOX_MATERIAL_METALLIC (0.0f)

/** Roughness of a surface without PbrMaterial and of a constructed PbrMaterial: matte, as in
 * glTF. A smooth or glossy surface sets it explicitly. */
#define BOX_MATERIAL_ROUGHNESS (1.0f)

/** Physically based surface parameters, default metallic 0 and roughness 1 (matte), inherited
 * by instances, like Rgb.
 *
 * A field a scene does not set keeps its default, so a PbrMaterial that only sets metallic is as
 * rough as a shape without PbrMaterial.
 */
ECS_STRUCT(PbrMaterial, {
    float metallic;  /**< 0 dielectric to 1 metal */
    float roughness; /**< 0 mirror to 1 fully diffuse */
});

/** Strength of a constructed ClearCoat: a full coat */
#define BOX_MATERIAL_CLEAR_COAT_STRENGTH (1.0f)

/** Roughness of a constructed ClearCoat: a mirror like lacquer */
#define BOX_MATERIAL_CLEAR_COAT_ROUGHNESS (0.0f)

/** Thin dielectric layer over a 3D mesh (car paint, lacquer), glTF's KHR_materials_clearcoat,
 * default strength 1 and roughness 0, inherited like Rgb; a field a scene does not set keeps its
 * default, both clamp to 0..1. Only 3D meshes have a coat, 2D shapes and text ignore it.
 *
 * A dielectric of f0 0.04 on the geometric normal, not the normal map: direct light adds its GGX
 * lobe times Fc = F(l.h) * strength over 1 - Fc of the base, indirect light its reflection by its
 * split sum albedo over the rest. Emission is not attenuated; an unlit view ignores the coat.
 *
 * Transitionable (Transition): strength and roughness interpolate linearly by the timing
 * function, through a strength of 0 too; adding or removing ClearCoat animates from and to a
 * strength of 0 with on_add and on_remove of TransitionSpec, else it snaps.
 */
ECS_STRUCT(ClearCoat, {
    float strength;  /**< 0 no coat to 1 full coat */
    float roughness; /**< Roughness of the coat, 0 mirror to 1 fully diffuse */
});

/** Strength of a constructed SpecularAnisotropy: a fully stretched highlight */
#define BOX_MATERIAL_ANISOTROPY_STRENGTH (1.0f)

/** Rotation of a constructed SpecularAnisotropy: along the +U direction of the texture */
#define BOX_MATERIAL_ANISOTROPY_ROTATION (0.0f)

/** Highlight of a 3D mesh stretched along a direction in its surface (brushed metal, satin),
 * glTF's KHR_materials_anisotropy, default strength 1 and rotation 0, inherited like Rgb.
 *
 * Applies to the base lobe in direct and indirect light, not the clear coat. The direction is
 * +U of the cotangent frame of the UVs turned by rotation, so a continuous highlight needs a
 * continuous UV mapping. 2D shapes, text and unlit views ignore it.
 *
 * Transitionable: strength and rotation interpolate linearly; adding or removing it animates
 * from and to a strength of 0 with on_add and on_remove of TransitionSpec, else it snaps.
 */
ECS_STRUCT(SpecularAnisotropy, {
    float strength; /**< 0 isotropic to 1 fully stretched, clamped to 0..1 */
    float rotation; /**< Radians counter clockwise from the +U direction of the texture, around
                     *   the normal, wrapped to 0..2 pi */
});

/** Roughness of a constructed Sheen: a rim halfway between tight and broad */
#define BOX_MATERIAL_SHEEN_ROUGHNESS (0.5f)

/** Soft bright rim at grazing angles of a 3D mesh (velvet, cloth, dust), glTF's
 * KHR_materials_sheen, default a white color and roughness 0.5, inherited like Rgb; a field a
 * scene does not set keeps its default. Only 3D meshes have it, 2D shapes and text ignore it.
 *
 * As three.js: direct light adds a Charlie lobe with the Neubelt visibility, indirect light the
 * diffuse light by a fit of the sheen albedo, and the base keeps 1 - 0.157 * max(color), with no
 * lookup table. It lies under the clear coat; emission is not scaled, an unlit view ignores it.
 *
 * Transitionable (Transition): color interpolates per channel like Rgb and roughness linearly by
 * the timing function, through a black color too; adding or removing Sheen animates from and to
 * a black color with on_add and on_remove of TransitionSpec, else it snaps.
 */
ECS_STRUCT(Sheen, {
    Rgb color;       /**< Color of the sheen, display encoded, black for none */
    float roughness; /**< 0 tight rim to 1 broad, clamped to 0..1 */
});

/** Factor of a constructed DiffuseTransmission: half of the diffuse light passes through */
#define BOX_MATERIAL_DIFFUSE_TRANSMISSION_FACTOR (0.5f)

/** Diffuse light passing through a thin 3D surface (leaf, paper, lampshade), glTF's
 * KHR_materials_diffuse_transmission, default factor 0.5, white, thickness 0 and blur 0.
 *
 * A Lambert lobe on the inverted normal, none on metals; the shape stays opaque and its shadow
 * is not tinted. The blur applies only with Transmission (frosted glass): what shows through is
 * blurred by its distance behind the surface, so a touching object stays sharp.
 *
 * Transitionable: factor, thickness and blur interpolate linearly and color like Rgb; adding or
 * removing it animates from and to a factor of 0 with on_add and on_remove, else it snaps.
 */
ECS_STRUCT(DiffuseTransmission, {
    float factor;    /**< 0 all diffuse light is reflected to 1 all of it passes through,
                      *   clamped to 0..1 */
    Rgb color;       /**< Tint of the light that passes through, display encoded */
    float thickness; /**< World units a ray travels through the surface for its shadow lookup,
                      *   0 for a thin sheet */
    float blur;      /**< World units of blur radius per world unit of distance behind the
                      *   surface (the tangent of the cone light spreads into) of a shape with
                      *   Transmission, 0 for none */
});

/** Emitted light, inherited by instances, like Rgb. Tints do not apply to it. */
ECS_STRUCT(Emissive, {
    float strength; /**< Emission intensity */
    Rgb color;      /**< Emission color */
});

/** Strength of a constructed Fresnel: the emission of an Emissive of strength 1 at a grazing
 * angle */
#define BOX_MATERIAL_FRESNEL_STRENGTH (1.0f)

/** Power of a constructed Fresnel: a thin rim, as the Schlick term */
#define BOX_MATERIAL_FRESNEL_POWER (5.0f)

/** Bias of a constructed Fresnel: nothing where the surface faces the viewer */
#define BOX_MATERIAL_FRESNEL_BIAS (0.0f)

/** Opacity of a constructed Fresnel: the rim adds no alpha */
#define BOX_MATERIAL_FRESNEL_OPACITY (0.0f)

/** Artistic rim of a 3D mesh where its surface turns away from the viewer (force fields,
 * holograms, ghost and selection glows, the rim light of stylized characters), default a white
 * color, strength 1, power 5, bias 0 and opacity 0, inherited like Emissive.
 *
 * The factor is f = saturate(bias + pow(1 - saturate(dot(n, v)), power)), with n the shading
 * normal after normal mapping and v toward the camera; back faces use the flipped normal, so a
 * two sided shape rims on both sides. color times strength times f adds to the emission, which
 * Tint does not change, and probe and reflection captures include it. opacity times f adds to
 * the alpha (up to 1) of a shape the blended pass draws (Opacity under 1 or a BlendMode); an
 * opaque shape ignores it. It is separate from the physical Fresnel of the specular lobes, which
 * it does not change.
 *
 * Needs Scene.fresnel: a scene without it draws the shape as if it had no Fresnel. Only 3D
 * meshes have it, 2D shapes and text ignore it.
 */
ECS_STRUCT(Fresnel, {
    Rgb color;      /**< Color of the rim, display encoded */
    float strength; /**< Emission intensity at a grazing angle, in the units of
                     *   Emissive.strength, 0 emits nothing */
    float power;    /**< Exponent of the falloff, higher for a thinner rim */
    float bias;     /**< Added to the factor before it is clamped, so a surface facing the viewer
                     *   can glow too */
    float opacity;  /**< Alpha added at the rim of a blended shape, 0..1 */
});

/** Index of refraction of a constructed Transmission and of a surface without one */
#define BOX_MATERIAL_IOR (1.5f)

/** Attenuation distance of a constructed Transmission and of a surface without one: light is
 * not absorbed */
#define BOX_MATERIAL_ATTENUATION_DISTANCE (1e10f)

/** Light passing through a 3D shape (glass), inherited by instances like Rgb, default factor 0,
 * ior 1.5, thickness 0, attenuation distance 1e10 and a white attenuation color.
 *
 * It draws in the sorted blended pass without depth writes, casts no shadow, is not captured and
 * ignores Opacity, BlendMode and AlphaCutoff. It shows a refracted, blurred and absorbed snapshot
 * of what its view drew before it, taken again at most 4 times a frame. A FogVolume between the
 * camera and the surface fogs the surface itself; the snapshot holds only the fog behind it.
 *
 * Transitionable: the four floats interpolate linearly and attenuation_color like Rgb; adding or
 * removing Transmission changes the batch at once.
 */
ECS_STRUCT(Transmission, {
    float transmission_factor;  /**< 0 opaque to 1 fully transmissive */
    float ior;                  /**< Index of refraction */
    float thickness_factor;     /**< World units a ray travels through the volume, times the
                                 *   scale */
    float attenuation_distance; /**< Distance at which light has the attenuation color */
    Rgb attenuation_color;      /**< Color of the light after attenuation_distance, display
                                 *   encoded */
});

/** Strength of a constructed Distortion: a fraction of the image height */
#define BOX_MATERIAL_DISTORTION_STRENGTH (0.004f)

/** Scale of a constructed Distortion: world units of one feature of the noise */
#define BOX_MATERIAL_DISTORTION_SCALE (1.0f)

/** Speed of a constructed Distortion: features per second */
#define BOX_MATERIAL_DISTORTION_SPEED (0.5f)

/** Moving shimmer of the scene behind a 3D mesh (heat haze, shield bubbles), inherited by
 * instances like Rgb, default strength 0.004, scale 1 and speed 0.5.
 *
 * It draws like Transmission: in the sorted blended pass without depth writes, casting no
 * shadow, not captured, ignoring Opacity, BlendMode and AlphaCutoff. It shows the snapshot of
 * what its view drew before it at the screen position of the fragment plus a screen space
 * offset: smooth noise that evolves in place, sampled at the object position (or the position
 * in the space of its TextureFrame with MappingFrame), so the pattern moves and rotates with the
 * shape. The noise follows the total render time, wrapped in double precision.
 *
 * The distorted scene replaces the diffuse light as a transmission factor of 1 does; specular
 * light, Emissive and Fresnel add on top. With Transmission the offset adds to its refraction
 * and its factor, absorption and blur apply; alone it samples the sharp snapshot. A shape with
 * Water ignores it.
 *
 * Its record is packed every frame while speed is not 0, like that of a TextureScroll.
 * Transitionable: the three floats interpolate linearly, on_add and on_remove start from and
 * end at a strength of 0. Needs Scene.distortion: a scene without it draws the shape as if it
 * had no Distortion. Only 3D meshes have it, 2D shapes and text ignore it.
 */
ECS_STRUCT(Distortion, {
    float strength; /**< Largest screen space offset of the scene behind, a fraction of the
                     *   image height */
    float scale;    /**< World units of one feature of the noise */
    float speed;    /**< How fast the pattern changes, in features per second */
});

/** Shallow color of a constructed Water, display encoded */
#define BOX_MATERIAL_WATER_SHALLOW_R (60)
#define BOX_MATERIAL_WATER_SHALLOW_G (170)
#define BOX_MATERIAL_WATER_SHALLOW_B (160)

/** Deep color of a constructed Water, display encoded */
#define BOX_MATERIAL_WATER_DEEP_R (8)
#define BOX_MATERIAL_WATER_DEEP_G (40)
#define BOX_MATERIAL_WATER_DEEP_B (64)

/** Depth of a constructed Water at which its body is about 95% deep_color, world units */
#define BOX_MATERIAL_WATER_DEPTH (3.0f)

/** Visibility of a constructed Water: world units along the view ray the scene behind shows
 * through */
#define BOX_MATERIAL_WATER_VISIBILITY (2.0f)

/** Absorption of a constructed Water, display encoded: white, the scene behind keeps its colors */
#define BOX_MATERIAL_WATER_ABSORPTION_R (255)
#define BOX_MATERIAL_WATER_ABSORPTION_G (255)
#define BOX_MATERIAL_WATER_ABSORPTION_B (255)

/** Refraction of a constructed Water, a fraction of the image height */
#define BOX_MATERIAL_WATER_REFRACTION (0.02f)

/** Foam color of a constructed Water, display encoded */
#define BOX_MATERIAL_WATER_FOAM_R (240)
#define BOX_MATERIAL_WATER_FOAM_G (245)
#define BOX_MATERIAL_WATER_FOAM_B (245)

/** Foam width of a constructed Water: the water depth below which foam appears, world units */
#define BOX_MATERIAL_WATER_FOAM_WIDTH (0.3f)

/** Foam scale of a constructed Water: the foam texture repeats as the UVs */
#define BOX_MATERIAL_WATER_FOAM_SCALE (1.0f)

/** Ripple scale of a constructed Water: no second normal sample */
#define BOX_MATERIAL_WATER_RIPPLE_SCALE (0.0f)

/** Scatter color of a constructed Water, display encoded: a turquoise */
#define BOX_MATERIAL_WATER_SCATTER_R (40)
#define BOX_MATERIAL_WATER_SCATTER_G (200)
#define BOX_MATERIAL_WATER_SCATTER_B (170)

/** Scatter of a constructed Water: no subsurface glow */
#define BOX_MATERIAL_WATER_SCATTER (0.0f)

/** Shore period of a constructed Water: no waves rolling in at the shore */
#define BOX_MATERIAL_WATER_SHORE_PERIOD (0.0f)

/** Shore reach of a constructed Water: the water depth at which a wave line forms, world units */
#define BOX_MATERIAL_WATER_SHORE_REACH (0.3f)

/** Surface of a body of water seen from above (ponds, lakes, canals, the sea), on a 3D mesh,
 * default a teal shallow and a dark blue deep color, depth 3, visibility 2, a white absorption,
 * refraction 0.02, an off white foam of width 0.3 without a texture, no ripples, no scatter
 * (a turquoise scatter_color) and no shore waves (a shore_reach of 0.3). Inherited
 * by instances like Rgb; a field a scene does not set keeps its default.
 *
 * It draws like Transmission: in the sorted blended pass without depth writes, casting no
 * shadow, not captured, over a snapshot of what its view drew before it, and it ignores
 * Transmission, Opacity, BlendMode, AlphaCutoff and TextureBlend.
 *
 * The depth d of a pixel is the distance from the surface to the opaque surface behind it along
 * the geometric normal, the ray length that along the view ray; with nothing behind (the sky)
 * both are infinite. The body color mixes shallow_color toward deep_color by
 * 1 - exp(-3 d / depth) and is lit diffusely by the light of the surface; it covers the scene
 * behind, refracted by refraction times the normal in screen space, by 1 - exp(-3 ray length /
 * visibility). The scene behind that shows through is tinted per channel by absorption (in
 * linear light) to the power of ray length / visibility, so deep or far water turns the floor
 * toward that color; white keeps its colors, and with nothing behind there is nothing to tint.
 * Below foam_width the opaque, diffusely lit foam replaces both by
 * saturate(1 - d / foam_width), broken up by the red of foam_texture. The direct and indirect
 * specular of PbrMaterial.roughness with an F0 of 0.02 (ior 1.33) adds on top.
 *
 * scatter adds the sunlight scattered inside the body, scatter_color times scatter times the
 * light of the first directional light (and its shadow): it glows through the crests of the
 * WaterWaves seen against the sun, most where the wave faces turn away from the sun toward the
 * viewer, and a little on the faces turned to the viewer, with a small part of the ambient light.
 * It is covered by the foam and by the scene behind like the body, and fogged by the aerial
 * perspective; 0 adds nothing.
 *
 * A shore_period above 0 makes waves lap at the shore every shore_period seconds: a thin line of
 * foam forms where the water depth d is shore_reach, rolls in faster and faster toward d = 0 and
 * turns into foam at the waterline, which washes back a little while the next line forms.
 *
 * The phase of the shore waves varies slowly along the shore (a low frequency noise of the world
 * XZ), so they do not arrive everywhere at once. Their lines add to the foam (the larger covers)
 * and are broken up by the foam texture and lit like it.
 *
 * With shore waves the record is packed every frame, the time wrapped in double precision like
 * that of a TextureScroll; a shore_period of 0 draws as without shore waves.
 *
 * Distant water keeps its sparkle: the slope variance that filtering takes away, of the minified
 * normal map (from the shortened length of its filtered normals, Toksvig) and of the WaterWaves
 * that fade out, widens the roughness (alpha squared grows by twice the variance) up to 0.5, or
 * PbrMaterial.roughness if that is larger. The sun shows a broad glitter path in the distance and
 * the reflections, the planar reflection and the blur of the scene behind soften there, while
 * close water, whose waves and normal map are resolved, keeps PbrMaterial.roughness.
 *
 * Waves are the PbrTextures.normal of the shape, scrolled by TextureScroll, under the travelling
 * waves of a WaterWaves if any; ripple_scale adds a second sample of it, moved by ripple_velocity
 * and whiteout blended. With a ripple_scale above 0 and a ripple_velocity its record is packed
 * every frame, like that of a TextureScroll; a still Water shares its record like the other
 * material components. With WaterFlow.textures the normal map and the foam texture travel with
 * the flow of the water instead of standing still (see WaterFlow).
 *
 * The foam texture is sampled twice, the second time at a different scale and rotation, and the
 * two are combined, so its lace does not read as one repeating tile.
 *
 * A WaterCaustics adds the moving web of sunlight that waves focus onto the floor seen through
 * the water.
 *
 * Underwater views and the back faces are not supported: the surface is seen from above. Needs
 * Scene.water: a scene without it draws the shape as if it had no Water. Only 3D meshes have it,
 * 2D shapes and text ignore it.
 */
ECS_STRUCT(Water, {
    Rgb shallow_color;      /**< Color of the body where it is shallow, display encoded */
    Rgb deep_color;         /**< Color of the body where it is deep, display encoded */
    float depth;            /**< Water depth at which the body is about 95% deep_color, world
                             *   units */
    float visibility;       /**< World units of water along the view ray through which the scene
                             *   behind still shows */
    Rgb absorption;         /**< Color the scene behind takes on through `visibility` world
                             *   units of water along the view ray, display encoded; white for
                             *   no tint */
    float refraction;       /**< Screen space distortion of the scene behind by the normal, a
                             *   fraction of the image height */
    Rgb foam_color;         /**< Color of the foam, display encoded */
    float foam_width;       /**< Water depth below which foam appears, world units, 0 for none */
    ecs_entity_t foam_texture; /**< Texture whose red breaks up the foam, sampled with the UVs of
                             *   the shape times foam_scale and again rotated at a coarser
                             *   scale; 0 for a smooth band */
    float foam_scale;       /**< Repeats of the foam texture per repeat of the UVs */
    float ripple_scale;     /**< Scale of the UVs of a second sample of the normal map, 0 for
                             *   none */
    vec2 ripple_velocity;   /**< Texture widths per second the second sample moves, wrapped
                             *   in double precision like TextureScroll */
    Rgb scatter_color;      /**< Color of the sunlight scattered inside the body, display
                             *   encoded */
    float scatter;          /**< Strength of the scattered sunlight, 0 for none */
    float shore_period;     /**< Seconds between two waves arriving at the shore, 0 for none */
    float shore_reach;      /**< Water depth at which a wave line forms, world units */
});

/** Waves a WaterWaves holds at most */
#define BOX_MATERIAL_WATER_WAVES (4)

/** Detail of a constructed WaterWaves: the normal map at its full weight */
#define BOX_MATERIAL_WATER_WAVES_DETAIL (1.0f)

/** Gravity of the deep water dispersion of a WaterWave without a speed, world units per second
 * squared */
#define BOX_MATERIAL_WATER_GRAVITY (9.81f)

/** One travelling Gerstner wave of a WaterWaves, all 0 when constructed: a disabled wave.
 *
 * The height of the wave at a world XZ point x is amplitude * sin(k dot(direction, x) - phase),
 * with k = 2 pi / wavelength; the phase grows with the time by k times the speed, plus k times
 * the WaterFlow velocity along the direction.
 */
ECS_STRUCT(WaterWave, {
    vec2 direction;   /**< Direction of travel in world XZ, normalized by the engine; zero (or not
                       *   finite) is +X */
    float wavelength; /**< World units between crests; at most 0 (or not finite) disables the
                       *   wave */
    float amplitude;  /**< World units, height of a crest above the mean surface */
    float steepness;  /**< 0..1, the Gerstner Q: 0 is a sine, 1 sharp crests and flat troughs;
                       *   clamped */
    float speed;      /**< World units per second the crests travel; 0 uses the deep water
                       *   dispersion sqrt(g wavelength / 2 pi) with g BOX_MATERIAL_WATER_GRAVITY */
});

/** Travelling waves of a Water (ponds, lakes, the sea): up to BOX_MATERIAL_WATER_WAVES Gerstner
 * waves whose analytic normal shades the surface, default no waves and a detail of 1. Inherited
 * by instances like Water; a field a scene does not set keeps its default.
 *
 * The waves change the shading normal only, the surface stays flat: its vertices, bounds and
 * culling do not move. Each fragment takes the normal of the sum of the waves at its world XZ,
 * in the frame the WaterFlow of the shape moves, and blends the normal map of the Water (its
 * PbrTextures.normal and ripples), weighted by detail, over it (whiteout blend). The refraction,
 * the planar reflection, the Fresnel and the glints follow that normal. A wave fades out where
 * it gets shorter than 4 pixels, so distant water does not shimmer; the slope variance of what
 * faded widens the roughness of the Water instead (see Water), so far water glitters rather
 * than turning into a mirror.
 *
 * The phases are integrated in double precision on the CPU every frame with the total render
 * time and wrapped to 0..2 pi, so a speed or a WaterFlow velocity that changes moves the waves
 * on smoothly, and the record of the shape is packed every frame like that of a TextureScroll.
 * At most BOX_RENDER_WATER_WAVE_SETS - 1 materials show their waves; later ones are flat.
 *
 * Needs Water and Scene.water: without them the component does nothing. Its fields have no
 * transition.
 */
ECS_STRUCT(WaterWaves, {
    WaterWave waves[4]; /**< The waves, BOX_MATERIAL_WATER_WAVES; a wavelength of 0 skips one */
    float detail;       /**< Weight of the normal map relative to the waves, 0 for the waves
                         *   alone; negative or not finite is 0 */
});

/** Wind speed of a constructed WaterSpectrum, meters per second at 10 meters above the water */
#define BOX_MATERIAL_SPECTRUM_WIND_SPEED (4.0f)

/** World units of the tile of the largest cascade of a constructed WaterSpectrum */
#define BOX_MATERIAL_SPECTRUM_SIZE (32.0f)

/** Cascades of a constructed WaterSpectrum */
#define BOX_MATERIAL_SPECTRUM_CASCADES (2)

/** Most cascades of a WaterSpectrum */
#define BOX_MATERIAL_SPECTRUM_CASCADES_MAX (3)

/** Texels per side of a cascade of a constructed WaterSpectrum */
#define BOX_MATERIAL_SPECTRUM_RESOLUTION (128)

/** Fewest texels per side of a cascade of a WaterSpectrum */
#define BOX_MATERIAL_SPECTRUM_RESOLUTION_MIN (64)

/** Most texels per side of a cascade of a WaterSpectrum */
#define BOX_MATERIAL_SPECTRUM_RESOLUTION_MAX (256)

/** Directionality of a constructed WaterSpectrum: most waves follow the wind */
#define BOX_MATERIAL_SPECTRUM_DIRECTIONALITY (0.8f)

/** Amplitude of a constructed WaterSpectrum: the spectrum of the wind as is */
#define BOX_MATERIAL_SPECTRUM_AMPLITUDE (1.0f)

/** Detail of a constructed WaterSpectrum: the normal map at a quarter of its weight */
#define BOX_MATERIAL_SPECTRUM_DETAIL (0.25f)

/** Wind driven waves of a Water from an ocean spectrum: hundreds of waves of different lengths
 * that each travel at their own speed (deep water dispersion), so the surface keeps changing
 * shape instead of sliding as one sheet. Default a light breeze on sheltered water: a wind of 4
 * m/s along +X, a tile of 32 world units, 2 cascades of 128 texels, a directionality of 0.8, an
 * amplitude of 1, a seed of 0 and a detail of 0.25. Inherited by instances like Water; a field a
 * scene does not set keeps its default. World units are taken as meters.
 *
 * The spectrum is a JONSWAP spectrum of a fully developed sea for the wind speed, spread around
 * the wind direction by directionality (cos^2 spreading toward the wind, the rest the same in
 * every direction), with the waves shorter than a centimeter damped. When its parameters change
 * the backend draws the initial amplitudes h0(k) of every wave of a square grid of wave numbers
 * from the seed; every frame it evolves them to the time with the deep water dispersion and
 * inverse FFTs the slopes and the height into a tiling texture per cascade, and builds its mips.
 * The tile of each next cascade is a fifth of the previous one, which takes over the waves from
 * 4 cycles per its tile on, so the cascades do not double up; the last one holds the waves
 * down to three of its texels long. The angular frequencies are multiples of 2 pi /
 * BOX_RENDER_SPECTRUM_PERIOD and the time is wrapped to that period in double precision, so the
 * waves never lose precision and repeat seamlessly.
 *
 * The waves change the shading normal only, the surface stays flat. Water fragments of a shape
 * with a WaterSpectrum add the slopes of all cascades, sampled at their world XZ in the frame the
 * WaterFlow of the Water entity of the spectrum moves (integrated in double precision), to those
 * of its WaterWaves, and blend the normal map over them weighted by detail (whiteout blend). The
 * slope variance that the mips of the cascades filter away widens the roughness like the faded
 * WaterWaves do (see Water), so distant water keeps glittering without aliasing. The height of
 * the spectrum adds to that of the WaterWaves for the scatter of the Water.
 *
 * A view binds the spectrum of the first WaterSpectrum of its scene: the shapes that inherit the
 * one component share it, a scene with WaterSpectrum components of more than one entity logs an
 * error once and shades all its WaterSpectrum shapes with the first. Needs Water and
 * Scene.water: without them the component does nothing. A disabled entity costs nothing. Its
 * fields have no transition.
 */
ECS_STRUCT(WaterSpectrum, {
    float wind_speed;       /**< Wind speed at 10 m above the water, m/s; 0 or less (or not
                             *   finite) for no waves */
    vec2 wind_direction;    /**< Direction the wind blows toward in world XZ, normalized by the
                             *   engine; zero (or not finite) is +X */
    float size;             /**< World units of the tile of the largest cascade; 0 or less (or
                             *   not finite) is the default */
    int32_t cascades;       /**< Cascades, clamped to 1..BOX_MATERIAL_SPECTRUM_CASCADES_MAX */
    int32_t resolution;     /**< Texels per side of a cascade, rounded to the nearest power of
                             *   two from BOX_MATERIAL_SPECTRUM_RESOLUTION_MIN to
                             *   BOX_MATERIAL_SPECTRUM_RESOLUTION_MAX */
    float directionality;   /**< 0..1, how much the waves follow the wind: 0 the same in every
                             *   direction, 1 none against the wind; clamped */
    float amplitude;        /**< Scale of the heights of the whole spectrum, 0 for flat water */
    uint32_t seed;          /**< Seed of the random amplitudes and phases of the waves */
    float detail;           /**< Weight of the normal map relative to the spectrum, 0 for the
                             *   spectrum alone; negative or not finite is 0 */
});

/** Velocity a Water surface moves with (a river, the water around a paddling boat), default
 * still, inherited by instances like Water. The WaterWaves of the shape travel in the frame
 * that moves with it: their phases integrate the velocity over the time.
 *
 * The field of a WaterRipples follows it too: its frame moves with the WaterFlow of its own
 * Water, and the ripples and foam over any other Water shape with a WaterFlow (the lengths of a
 * bending brook, each flowing along its chord) drift with the velocity of that shape, blended
 * smoothly across the seams between shapes. A RippleSource pushes relative to that local flow.
 * Without WaterWaves, a field or textures it only stores the velocity; without Water it does
 * nothing.
 *
 * With textures the normal map of the Water (PbrTextures.normal and its ripple_scale sample) and
 * its foam_texture travel with the flow by two phase flow mapping: each texture is sampled twice,
 * moved along the local velocity of the water by two sawtooth phases half a period of
 * BOX_RENDER_WATER_FLOW_PERIOD seconds apart, and cross faded so each sample resets while it is
 * invisible; a smooth noise over the world XZ offsets the phase per pixel, so the reset does not
 * pulse over the whole surface. The ripples and the foam lace move downstream and bend with the
 * water instead of standing still. The local velocity, taken in the texture space of the shape,
 * is the velocity of the field of the WaterRipples of the view, averaged over about a world
 * unit, where a fragment lies inside its window (and within its height band), so it is continuous
 * across the seams between Water shapes and follows the bends, and the velocity of this WaterFlow elsewhere, faded between the two over
 * the border band of the window like the ripple normals. Shapes that share their texture space
 * (a TextureMapping of MappingWorld) show no seam between them. A TextureScroll of the shape still
 * applies on top; scenes that move their textures with the flow drop it. With textures the
 * record of the shape is packed every frame, like that of a TextureScroll. Without textures
 * (the default) the textures do not move with the flow.
 */
ECS_STRUCT(WaterFlow, {
    vec2 velocity; /**< World units per second (X, Z) the surface moves with */
    bool textures; /**< Whether the normal map and foam texture of the Water move with the flow */
});

/** Strength of a constructed WaterCaustics: the pattern at its full contrast */
#define BOX_MATERIAL_WATER_CAUSTICS_STRENGTH (1.0f)

/** Scale of a constructed WaterCaustics: world units of one cell of the pattern */
#define BOX_MATERIAL_WATER_CAUSTICS_SCALE (1.0f)

/** Speed of a constructed WaterCaustics, cells per second */
#define BOX_MATERIAL_WATER_CAUSTICS_SPEED (0.5f)

/** Depth of a constructed WaterCaustics: water depth at which they are gone, world units */
#define BOX_MATERIAL_WATER_CAUSTICS_DEPTH (2.0f)

/** Caustics of a Water: the bright, moving web of sunlight that the waves focus onto the floor,
 * default a strength of 1, cells of 1 world unit, a speed of 0.5 and a depth of 2. Inherited by
 * instances like Water; a field a scene does not set keeps its default.
 *
 * They light the scene seen through the water (the floor, a submerged hull or stone), at the
 * world position of what lies behind each fragment; whatever is above the water and the sky
 * show none. The pattern is two layers of animated, slightly swirled cellular edge noise of
 * different scales: thin curved bright lines on a slightly darker floor with a mean of about 1,
 * sampled where the sunlight that reaches the point entered the water: the point moved toward
 * the first directional light along its direction refracted by the surface (ior 1.33) by the
 * water depth, so the pattern shifts as the sun moves. Red, green and blue refract slightly
 * apart.
 *
 * The color seen through the water is multiplied by 1 + strength * (c - 1) * k, with c the
 * pattern and k the share of the point lit by the first directional light of the scene: its
 * cascaded shadow (and LightCookie) there times how far the light is above the horizon. A floor
 * in shadow, a scene without a directional light or with the light below the horizon shows
 * none. With the water depth the lines widen and dim and the pattern fades, gone at depth. Foam
 * covers them as it covers the floor.
 *
 * The pattern lives in the frame the WaterFlow of the shape moves (the integral of its velocity
 * in cells, kept in double precision on the CPU and wrapped), so it drifts with the water; it
 * changes with the total render time times speed, wrapped in double precision. With a WaterFlow
 * or a speed other than 0 the record of the shape is packed every frame, like that of a
 * TextureScroll.
 *
 * Needs Water and Scene.water: without them the component does nothing. A strength of 0 (or
 * negative or not finite) draws as without one. Its fields have no transition.
 */
ECS_STRUCT(WaterCaustics, {
    float strength;   /**< How strongly the pattern brightens and darkens the floor, 0 for none */
    float scale;      /**< World units of one cell of the pattern; at most 0 or not finite is
                       *   the default */
    float speed;      /**< How fast the pattern changes, about cells per second, 0 for a still
                       *   pattern */
    float depth;      /**< Water depth over which the caustics blur and fade out, world units */
});

/** World units of the field of a constructed WaterRipples */
#define BOX_MATERIAL_RIPPLES_SIZE (32.0f)

/** Cells per side of the field of a constructed WaterRipples */
#define BOX_MATERIAL_RIPPLES_RESOLUTION (256)

/** Fewest cells per side of a field of WaterRipples */
#define BOX_MATERIAL_RIPPLES_RESOLUTION_MIN (64)

/** Most cells per side of a field of WaterRipples */
#define BOX_MATERIAL_RIPPLES_RESOLUTION_MAX (1024)

/** Wave speed of a constructed WaterRipples, world units per second */
#define BOX_MATERIAL_RIPPLES_WAVE_SPEED (2.0f)

/** Damping of a constructed WaterRipples: half the ripple energy is lost per second */
#define BOX_MATERIAL_RIPPLES_DAMPING (0.5f)

/** Strength of a constructed WaterRipples: the ripple normals at their full weight */
#define BOX_MATERIAL_RIPPLES_STRENGTH (1.0f)

/** Foam decay of a constructed WaterRipples: 40% of the foam fades per second */
#define BOX_MATERIAL_RIPPLES_FOAM_DECAY (0.4f)

/** Foam spread of a constructed WaterRipples, world units squared per second */
#define BOX_MATERIAL_RIPPLES_FOAM_SPREAD (0.02f)

/** Half size of a constructed RippleSource in its own X and Z, world units */
#define BOX_MATERIAL_RIPPLE_SOURCE_EXTENT (0.5f)

/** Strength of a constructed RippleSource */
#define BOX_MATERIAL_RIPPLE_SOURCE_STRENGTH (1.0f)

/** Interactive ripples of a Water: a square height field simulated on the GPU around an entity,
 * in which RippleSource entities moving through the water leave a wake and
 * box_material_ripple_at makes rings, default 32 world units of 256 cells centered on the Water
 * entity, a wave speed of 2, a damping of 0.5, a strength of 1, a foam decay of 0.4, a foam
 * spread of 0.02 and no crest foam. On the Water shape entity.
 *
 * The heights follow the damped wave equation in fixed steps of BOX_RENDER_RIPPLE_STEP seconds of
 * the frame delta time (at most BOX_RENDER_RIPPLE_SUBSTEPS a frame), so a run with a fixed delta
 * time gives the same field every time. The wave speed is clamped so the steps stay stable for
 * any size and resolution.
 *
 * The field lives in the frame of the water that the WaterFlow of its Water moves (the integral
 * of its velocity, kept in double precision): over that Water ripples drift with the water without
 * being resampled. The window of the field keeps centered on follow by whole cells with wrapping
 * addressing; cells that enter it start flat, the others keep their heights. The heights fade to
 * flat toward the border of the window, so waves do not reflect off it.
 *
 * The field follows the local flow of the water under it: every frame it takes the Water shapes of
 * its scene with a WaterFlow whose oriented XZ rectangle (world transform and mesh bounds)
 * overlaps its window and whose center lies within half a world unit of the height of its Water,
 * BOX_RENDER_RIPPLE_FLOWS at most, the nearest to the center of the window first. The velocity of
 * a cell is the weighted average of the velocities of the rectangles covering it, each weight
 * fading out smoothly over BOX_RENDER_RIPPLE_FLOW_FADE world units inside the edges of its
 * rectangle, so it is continuous across the seams between overlapping shapes (the shape of the
 * Water of the field is one of them, so a run across a flowing lagoon blends with the lagoon); a
 * cell no rectangle covers takes the WaterFlow of the Water of the field. Every step the heights
 * and the foam are advected by that velocity relative to the frame (semi-Lagrangian, bilinear,
 * looking back BOX_RENDER_RIPPLE_FLOW_REACH cells a step at most), so foam laid down in a bending
 * brook drifts downstream along its bends at the speed of each length. Where the water flows
 * with the frame nothing is resampled: a field over one uniformly flowing Water is the same as
 * without the other shapes. Flow does not divert around obstacles.
 *
 * The field has a foam channel next to the heights, 0..1 per cell: RippleSource.foam lays foam
 * down, box_material_foam_at splashes it, and where the slope of the heights is steeper than
 * BOX_RENDER_RIPPLE_CREST_SLOPE crest_foam forms more. Every step the foam spreads to its
 * neighbours by foam_spread and fades by foam_decay; it drifts with the water frame like the
 * heights. Water fragments in the window draw it like the shore foam of the Water (its
 * foam_color, the larger of the two covering), broken up by the Water.foam_texture. A field
 * without foam draws as without the channel.
 *
 * Water fragments inside the window, within half a world unit of the height of the Water entity,
 * add the normal of the field (bilinear heights, central differences) to their shading normal,
 * on top of the WaterWaves, scaled by strength and faded out toward the border; the refraction,
 * the reflections and the specular follow. A view binds the field of the first WaterRipples of
 * its scene, a scene with more logs an error once and simulates only that one.
 *
 * Needs Water and Scene.water: without them the component does nothing. A disabled entity, a size
 * of 0 or less (or not finite) has no field and costs nothing.
 */
ECS_STRUCT(WaterRipples, {
    float size;             /**< World units of the side of the square field */
    int32_t resolution;     /**< Cells per side, rounded to the nearest power of two from
                             *   BOX_MATERIAL_RIPPLES_RESOLUTION_MIN to
                             *   BOX_MATERIAL_RIPPLES_RESOLUTION_MAX */
    ecs_entity_t follow;    /**< Entity the field stays centered on, 0 for the Water entity */
    float wave_speed;       /**< World units per second the ripples spread */
    float damping;          /**< Share of the ripple energy lost per second, 0..1 */
    float strength;         /**< Scale of the ripple normals in the shading */
    float foam_decay;       /**< Share of the foam that fades per second, 0..1 */
    float foam_spread;      /**< World units squared per second the foam diffuses to its
                             *   neighbours, 0 for none; clamped so the steps stay stable */
    float crest_foam;       /**< Foam formed per second per unit of slope of the heights above
                             *   BOX_RENDER_RIPPLE_CREST_SLOPE, 0 for none */
});

/** Something that moves through the water (a boat, a swimmer, a paddle), default an ellipse of
 * half size 0.5 by 0.5 and a strength of 1. On any entity with a world transform.
 *
 * Every step of the field of WaterRipples of its scene that its ellipse overlaps, at its world
 * position and yaw (its extents scaled by its world scale), the water under it sinks in
 * proportion to its speed relative to the water: its world velocity, from the change of its world
 * position between frames, minus the local flow of the field at its position (the velocity of its
 * cell, see WaterRipples). A boat held still on flowing water leaves a wake, stronger in a fast
 * run than in a slow pool; a rock that flows with the water none; one that moves faster than
 * the wave speed leaves a V shaped wake. With foam it also lays foam down in the foam channel of
 * the field, in proportion to the same speed, so it leaves a foaming trail that drifts and fades
 * with the water. A field takes BOX_RENDER_RIPPLE_SOURCES at most.
 */
ECS_STRUCT(RippleSource, {
    vec2 extents;           /**< Half size in its own X and Z, world units, an ellipse */
    float strength;         /**< Height pushed per unit of speed through the water: the water at
                             *   the center sinks by 0.05 strength world units per world unit
                             *   the source travels through it */
    float foam;             /**< Foam laid down per unit of speed through the water: the foam
                             *   at the center grows by 2 foam per world unit the source travels
                             *   through it (1 is solid foam), 0 for none */
});

/** A disturbance of the field of a Water that box_material_ripple_at queued */
typedef struct box_material_ripple_t {
    ecs_entity_t water;     /**< Entity with the Water and WaterRipples */
    float position[3];      /**< World position of the center */
    float radius;           /**< World units of the radius */
    float strength;         /**< World units the water at the center sinks */
    float foam;             /**< Foam added at the center, 0..1 is a share of solid foam */
} box_material_ripple_t;

/** Disturb the field of the WaterRipples of a Water once, for rings from splashes and paddle
 * strokes. The water within radius of the position sinks by strength at the center, smoothly less
 * toward the radius, on the next step of the field; a ring spreads from there. Scripts call it as
 * box.material.ripple_at(water, position, radius, strength).
 *
 * @param world The world
 * @param water Entity with Water and WaterRipples
 * @param position World position of the center, XZ is used
 * @param radius World units of the radius, above 0
 * @param strength World units the water sinks at the center, negative raises it
 * @return Whether it was queued; an error is logged for a water without WaterRipples or an
 * argument that is not finite or a radius of 0 or less
 */
bool box_material_ripple_at(
    ecs_world_t *world,
    ecs_entity_t water,
    const float *position,
    float radius,
    float strength);

/** Splash foam onto the field of the WaterRipples of a Water once, for spray and breaking waves.
 * The foam within radius of the position grows by amount at the center, smoothly less toward the
 * radius, on the next step of the field; it then drifts, spreads and fades like the wake foam of
 * a RippleSource. Scripts call it as box.material.foam_at(water, position, radius, amount).
 *
 * @param world The world
 * @param water Entity with Water and WaterRipples
 * @param position World position of the center, XZ is used
 * @param radius World units of the radius, above 0
 * @param amount Foam added at the center, 1 is solid foam, negative clears it
 * @return Whether it was queued; an error is logged like for box_material_ripple_at
 */
bool box_material_foam_at(
    ecs_world_t *world,
    ecs_entity_t water,
    const float *position,
    float radius,
    float amount);

/** Get the disturbances box_material_ripple_at and box_material_foam_at queued and the fields
 * did not apply yet, in call order, which the render extract removes once it applied them.
 *
 * @param world The world
 * @return The box_material_ripple_t, NULL before box.material was imported
 */
ecs_vec_t* box_material_ripples_queue(
    const ecs_world_t *world);

/** Equation the fragments of a material are combined with, default Opaque. A shape with
 * Transmission or Water ignores it. */
ECS_ENUM(BlendMode, {
    Opaque = 0,             /**< No blending, the fragment replaces the target */
    AlphaBlend = 1,         /**< Source over, the fragment is weighted by its alpha */
    PremultipliedAlpha = 2, /**< Source over with a color that already holds its alpha */
    Additive = 3,           /**< The fragment is added to the target */
    Multiply = 4,           /**< The fragment multiplies the target */
    Screen = 5,             /**< Inverse multiply, the target is brightened */
    Subtractive = 6,        /**< The fragment is subtracted from the target */
    Min = 7,                /**< The darker of the fragment and the target */
    Max = 8                 /**< The brighter of the fragment and the target */
});

/** What the faces pointing away from the camera show, default BackFaceCull */
ECS_ENUM(BackFaceMode, {
    BackFaceCull = 0,     /**< Nothing, the faces are culled */
    BackFacePlain = 1     /**< The material constants without any texture */
});

/** What the samplers of a shape's PbrTextures do with UVs outside the 0..1 range */
ECS_ENUM(TextureWrap, {
    WrapRepeat = 0, /**< The texture tiles */
    WrapClamp = 1,  /**< The edge texel is held */
    WrapMirror = 2  /**< Every other tile is mirrored */
});

/** Scale and offset applied to the UVs of a material, defaults to 1, 1, 0, 0, inherited by
 * instances, like Rgb.
 *
 * A scale of 4 tiles the texture four times across the shape (with WrapRepeat); the offset
 * shifts the texture after the scale, in texture widths. UV 0, 0 is the top left of an image.
 */
ECS_STRUCT(TextureTransform, {
    float scale_x;  /**< U multiplier */
    float scale_y;  /**< V multiplier */
    float offset_x; /**< U addend, applied after the scale */
    float offset_y; /**< V addend, applied after the scale */
});

/** Velocity of a constructed TextureScroll, in texture widths per second: still */
#define BOX_MATERIAL_SCROLL_VELOCITY (0.0f)

/** Endless scrolling of the UVs of a 3D mesh (water, lava, conveyor belts, force fields),
 * default velocities of 0, inherited by instances like TextureTransform. Only 3D meshes scroll,
 * 2D shapes and text ignore it.
 *
 * Every PbrTextures layer moves by velocity times the total render time (that of LightCookie),
 * after TextureMapping and TextureTransform, whose offset and transitions it adds to; the normal
 * map moves by normal_velocity on top. Offsets wrap in double precision to 0..2 (WrapMirror).
 *
 * Its record is packed every frame; a capture sees the offset of its frame and is not redone for
 * the motion. TextureBlend layers keep their TextureTransform and do not scroll; the velocity of
 * the shape moves its mask and layers like its offset does, normal_velocity does not.
 *
 * Needs Scene.texture_scroll: a scene without it draws the shape as if it had no TextureScroll
 * and does not pack its record every frame.
 */
ECS_STRUCT(TextureScroll, {
    vec2 velocity;        /**< Texture widths per second added to the UV offset of every layer */
    vec2 normal_velocity; /**< Texture widths per second added to the UV offset of the normal map
                           *   only, on top of velocity */
});

/** Hides the parts of a 3D mesh whose per vertex data channel is below a threshold, a material
 * component inherited by instances like Rgb, default channel 0, threshold 0, softness 0, not
 * inverted.
 *
 * channel picks one of the 8 floats of the per vertex data of the mesh (Mesh3.data0 channels 0
 * to 3, data1 channels 4 to 7, box_mesh_vertex_data). Across a triangle the value of the channel
 * is interpolated between its vertices; where it is below threshold (above with invert) the
 * fragment is discarded. softness widens the edge into a dithered band of softness channel
 * units above the threshold, in which ever more pixels are kept. A part whose vertices are all
 * below the threshold is not drawn at all; a value equal to the threshold is on the edge.
 *
 * The shapes draw in the alpha tested path (as with an AlphaCutoff, so a scene needs
 * Scene.alpha_cutout), in the depth prepass, the forward pass, the shadows of every light,
 * planar reflections and captures alike. The threshold is in the material record: writing it
 * animates every shape that shares the material without rebuilding meshes or moving shapes
 * between batches. A mesh without per vertex data, a 2D shape and text ignore it.
 */
ECS_STRUCT(VertexReveal, {
    int32_t channel;        /**< Data channel 0 to 7: data0.x to data0.w, then data1.x to
                             *   data1.w; out of range reveals everything */
    float threshold;        /**< Value below which the mesh is hidden */
    float softness;         /**< Width in channel units of the dithered edge above the threshold,
                             *   0 for a hard edge */
    bool invert;            /**< Hide above the threshold instead, the edge below it */
});

/** Default data channel of a constructed VertexOcclusion, the occlusion channel of foliage
 * (BOX_MESH_DATA_OCCLUSION) */
#define BOX_MATERIAL_OCCLUSION_CHANNEL (7)

/** Occlusion of the indirect light of a 3D mesh by one of its per vertex data channels, a
 * material component inherited by instances like Rgb, default channel 7 and strength 1.
 *
 * channel picks one of the 8 floats of the per vertex data of the mesh (Mesh3.data0 channels 0
 * to 3, data1 channels 4 to 7, box_mesh_vertex_data), the occlusion of the vertex in 0..1: 1 is
 * open, 0 fully occluded, as the CrownOcclusion node of a mesh graph writes it for the depth of
 * a leaf in its crown. Across a triangle it is interpolated between the vertices. The forward
 * pass multiplies the occlusion of the material (its PbrTextures and the view's ambient
 * occlusion) by 1 - strength (1 - value), on front and back faces, so it darkens the indirect
 * diffuse and specular light only; the direct light and the shadows are left alone.
 *
 * It applies to the materials whose texture coordinates are the UVs of the mesh (MappingUv),
 * which foliage cards have; a projected TextureMapping ignores it, as do meshes without per
 * vertex data, 2D shapes and text. Its record is in the material table: writing it changes the
 * shading of every shape that shares the material without rebuilding meshes or batches.
 */
ECS_STRUCT(VertexOcclusion, {
    int32_t channel;        /**< Data channel 0 to 7: data0.x to data0.w, then data1.x to
                             *   data1.w; out of range for none */
    float strength;         /**< How much of the occlusion applies, clamped to 0..1 */
});

/** Height of a constructed WindSway: world units above base at which the bend is full */
#define BOX_MATERIAL_SWAY_HEIGHT (1.0f)

/** Flutter frequency of a constructed WindSway, in Hz */
#define BOX_MATERIAL_SWAY_FLUTTER_FREQUENCY (2.0f)

/** Flutter reach of a constructed WindSway: world distance from the origin of the shape over
 * which the flutter rises from 0 to full */
#define BOX_MATERIAL_SWAY_FLUTTER_REACH (0.3f)

/** Branch frequency of a constructed WindSway, in Hz: the sway of the branches of level 1 */
#define BOX_MATERIAL_SWAY_BRANCH_FREQUENCY (0.5f)

/** Deepest branch level a WindSway with pivots tells apart; deeper levels sway as this one */
#define BOX_MATERIAL_SWAY_LEVELS (4)

/** Cells per object space unit of the grid the pivots of a WindSway with pivots snap down to */
#define BOX_MATERIAL_SWAY_GRID (256.0f)

/** Bias of the offsets of the ancestor pivots of a WindSway with pivots in grid cells: an
 * ancestor lies at most this many cells, 128 units, from the parent along each axis */
#define BOX_MATERIAL_SWAY_BIAS (32768)

/** Bending and fluttering of the vertices of a 3D mesh in the Wind (trees, bushes, grass), a
 * material component inherited by instances like Rgb, default bend 0, height 1, base 0, flutter
 * 0, flutter_frequency 2, flutter_reach 0.3, no pivots, branch 0 and branch_frequency 0.5.
 *
 * The main bend is a smooth function of the world position of a vertex only: its height above
 * base over height, clamped to 0..1 and squared, times bend times the strength of the wind at
 * its world X and Z (the speed, varied by the gusts and the sway of the Wind). Every shape of a
 * plant (trunk, branches, leaf cards as separate child shapes) with the same bend, height and
 * base so moves together without cracks. A vertex that moves sideways is lowered so branches do
 * not visibly stretch, and the normals bend with the displacement.
 *
 * The flutter adds a fast motion of flutter world units at full wind (BOX_WIND_FULL_SPEED) at
 * flutter_frequency, different for every vertex, weighted by the world distance of the vertex
 * from the origin of its shape over flutter_reach, clamped to 0..1: a leaf card built with its
 * origin at its stem stays attached there.
 *
 * The displacement is the same in every pass that places the vertices: the depth prepass, the
 * forward pass, the shadows of every light, planar reflections and captures. The culling bounds
 * grow by the largest displacement so swaying shapes do not pop, and the shadow tiles and
 * cascades with swaying casters render again every frame while the wind blows.
 *
 * With pivots, a mesh with per vertex data (box_mesh_vertex_data, box_mesh_vertex_data_upper)
 * sways like a hierarchy of shapes, so one merged tree mesh moves like a trunk with separate
 * branches and leaves. The data of each vertex holds (box_wind_sway_vertex packs it):
 *
 * - data0.xyz: the pivot of the branch the vertex belongs to, in object space (where the branch
 *   joins its parent)
 * - data0.w: the level of that branch in its integer part: 0 for the trunk, which only bends, 1
 *   for the branches on the trunk, 2 for the branches on those, up to BOX_MATERIAL_SWAY_LEVELS;
 *   plus 0.5 times the flutter weight of the vertex, 0..1, the share of the flutter it moves by:
 *   0 at the stem of a leaf, 1 at its tip
 * - data1: unused, free for a VertexReveal or VertexOcclusion
 * - data2.xyz: the pivot of the parent branch, in object space, the same value as the data0.xyz
 *   of the vertices of the parent; unused below level 2
 * - data2.w: the stiffnesses of the branch, its parent, grandparent and great grandparent, each
 *   0..1 as round(63 stiffness) in 6 bits, the branch in the lowest: a branch with a stiffness
 *   of 1 does not swing about its pivot
 * - data3: the pivots of the grandparent (level 3 and up) and of the great grandparent (level 4)
 *   as offsets from the pivot of the parent in cells of 1 / BOX_MATERIAL_SWAY_GRID units, each
 *   plus BOX_MATERIAL_SWAY_BIAS in 16 bits, the six of them in this order packed 24 bits to a
 *   channel as whole numbers
 *
 * Every pivot snaps down to the grid of BOX_MATERIAL_SWAY_GRID cells per unit, the same in the
 * vertices of the branch and in those of its descendants.
 *
 * Every vertex of a branch of level 1 or more turns rigidly about its pivot: about the horizontal
 * axis across the wind by branch radians times (1 - stiffness) at full wind, leaning with the
 * wind and swinging at level times branch_frequency, plus a smaller swing about the wind axis.
 * The phase of the swing is hashed from the pivot, so neighbouring branches swing apart. The
 * strength of the wind is read at the pivot, so a branch never stretches. A branch of level 2 or
 * more then turns with each of its ancestors about the pivot of the ancestor, its parent first,
 * out to the branch of level 1, so a branch of every level stays attached to its parent; a leaf
 * takes the pivot and level of a branch one level deeper than the twig it grows on, or those of
 * the twig with a flutter weight. The main bend then moves the vertex by its rest position as
 * without pivots, and the flutter is weighted by the flutter weight instead of the distance from
 * the origin. Normals turn with the branch. A mesh without data sways as without pivots.
 * With foliage the ancestors are not read: every vertex turns about the pivot in data0 with a
 * stiffness of 0 and without its parent.
 *
 * Its record is packed every frame, like that of a TextureScroll. Needs Scene.wind: a scene
 * without it draws the shape as if it had no WindSway, with the shaders it has without it. Only
 * 3D meshes sway, 2D shapes and text ignore it.
 */
ECS_STRUCT(WindSway, {
    float bend;             /**< World units a vertex at height moves per unit of wind speed,
                             *   0 for a rigid shape */
    float height;           /**< World height above base at which the bend reaches its full
                             *   amount; at least 0.001 */
    float base;             /**< World Y at which the displacement is 0, the ground */
    float flutter;          /**< World units of the fast local flutter at full wind, 0 for
                             *   none */
    float flutter_frequency; /**< Hz of the flutter at full wind, slower in a light wind */
    float flutter_reach;    /**< World distance from the origin of the shape over which the
                             *   flutter rises from 0 to full; at least 0.001; unused with pivots
                             *   on a mesh with data, whose flutter is weighted by data0.w */
    bool pivots;            /**< Read the pivot, level, phase, stiffness and flutter weight of
                             *   every vertex from the per vertex data of the mesh */
    float branch;           /**< Radians a branch with pivots and a stiffness of 0 turns about
                             *   its pivot at full wind, 0 for rigid branches */
    float branch_frequency; /**< Hz of the swing of the branches of level 1 at full wind,
                             *   slower in a light wind; level n swings n times as fast */
    bool foliage;           /**< With pivots, read only data0 of the per vertex data, the pivot
                             *   of the twig, its level and the flutter weight, as the foliage
                             *   of a MeshGraph (Cards) holds it: data1, the seed, hue, growth
                             *   and occlusion of the cards, is not read as a parent pivot or
                             *   stiffness; every vertex turns with a stiffness of 0 about the
                             *   pivot of its twig only, like the twig, so the stems stay on it */
});

/** Pack the per vertex data a WindSway with pivots reads for a vertex of a branch. The pivots
 * are stored snapped down to the grid of BOX_MATERIAL_SWAY_GRID, so box_wind_sway_ancestors
 * unpacks the pivot of an ancestor as the same floats the ancestor's own data0 holds.
 *
 * @param data0 Receives the snapped pivot and the level and flutter weight
 * @param data2 Receives the snapped parent pivot and the stiffnesses
 * @param data3 Receives the grandparent and great grandparent pivots
 * @param pivot Object space pivot of the branch of the vertex
 * @param level Level of the branch, 0 for the trunk, clamped to 0..BOX_MATERIAL_SWAY_LEVELS
 * @param weight Flutter weight of the vertex, 0..1
 * @param ancestors Object space pivots of the parent, grandparent and great grandparent, the
 *   first level - 1 of them (at most 3) read; NULL below level 2
 * @param stiffness Stiffnesses of the branch, its parent, grandparent and great grandparent,
 *   0..1, the first level of them (at most 4) read; NULL for 0
 */
void box_wind_sway_vertex(
    vec4 data0,
    vec4 data2,
    vec4 data3,
    const vec3 pivot,
    int32_t level,
    float weight,
    const vec3 *ancestors,
    const float *stiffness);

/** Unpack the pivots of the ancestors of a vertex from the data box_wind_sway_vertex packs, as
 * a WindSway with pivots does: snapped to the grid of BOX_MATERIAL_SWAY_GRID.
 *
 * @param data2 The parent pivot and the stiffnesses
 * @param data3 The grandparent and great grandparent pivots
 * @param ancestors Receives the pivots of the parent, grandparent and great grandparent
 */
void box_wind_sway_ancestors(
    const vec4 data2,
    const vec4 data3,
    vec3 ancestors[3]);

/** Where the texture coordinates of a material come from */
ECS_ENUM(TextureMappingMode, {
    MappingUv = 0,      /**< The UVs of the mesh, the default */
    MappingObject = 1,  /**< Projected from the object space position times the instance scale,
                         *   relative to the scale of the nearest TextureFrame if any */
    MappingWorld = 2,   /**< Projected from the world position, fixed in the world */
    MappingFrame = 3    /**< Projected from the position in the space of the nearest
                         *   TextureFrame, the shape or an ancestor; moves and scales with
                         *   that frame */
});

/** How the texture coordinates of a material are made, default MappingUv with a size of 1 and a
 * blend of 0, inherited by instances, like TextureTransform.
 *
 * MappingObject projects the object space position times the instance scale (the world scale
 * of each axis, divided by the uniform world scale of the nearest TextureFrame if any, so the
 * textures scale with a scaled model), MappingWorld the world position, MappingFrame the
 * position in the local units of the nearest TextureFrame (its scale included, so a frame
 * scaled by 10 shows its textures 10x larger; as MappingWorld without one), divided by size,
 * onto the plane of the dominant normal axis, oriented as the faces of a Box. A blend above 0
 * mixes the three projections (triplanar) to hide seams.
 *
 * TextureTransform applies after the mapping. Parametric surfaces keep their own UVs, except a
 * Sweep with MappingObject (see Sweep) and MappingFrame, which projects them too.
 */
ECS_STRUCT(TextureMapping, {
    TextureMappingMode mode; /**< Source of the texture coordinates */
    float size;         /**< World units one repeat of the texture covers with MappingObject
                         *   and MappingWorld, default 1; a size that is not positive and finite
                         *   is 1 */
    float blend;        /**< Width of the triplanar blend, 0..1, default 0 for the hard
                         *   projection on the dominant axis; clamped to 0..1, not finite is 0 */
});

/** Texture entities for each PBR layer, 0 for none, inherited by instances like Rgb, part of the
 * batch of a shape, not the material table, so PbrTextures brings no MaterialId. Minified color
 * layers average linear light, ORM and normal stored values, a texture shared by both as color.
 *
 * A texture entity has a TexturePath (box/texture.h) or is a Surface's texture, sampling its
 * image of the same frame; until loaded or when missing the layer has no effect. BackFacePlain
 * back faces sample no textures, except the albedo alpha of a shape with AlphaCutoff.
 *
 * The ORM layer packs glTF's occlusion and metallic roughness textures, stored, not display
 * encoded: green multiplies PbrMaterial.roughness, blue PbrMaterial.metallic, red is the
 * indirect light occlusion (1 for unoccluded). A missing layer is white, a no-op.
 */
ECS_STRUCT(PbrTextures, {
    ecs_entity_t albedo;    /**< Multiplied with the base color and opacity, display encoded;
                             *   its alpha only shows with a blend mode or AlphaCutoff. Any layer
                             *   samples a TexturePath.single_channel one as (r, 0, 0, 1), stored */
    ecs_entity_t emissive;  /**< Multiplied with the Emissive color times its strength */
    ecs_entity_t orm;       /**< Occlusion (red), roughness (green) and metallic (blue), stored */
    ecs_entity_t normal;    /**< Tangent space normal map, green points up the image */
});

/** Blend of up to four texture layers weighed by a mask (terrain, a path worn into turf), for
 * 3D shapes, inherited by instances and part of the batch of a shape like PbrTextures.
 *
 * The red, green, blue and alpha of the mask, normalized, weigh the PbrTextures of layers 0..3,
 * each with its own TextureTransform; where the sum is 0 layer 0 shows. The PbrTextures of the
 * shape are ignored except emissive; its Rgb, PbrMaterial, Tint and Emissive still apply.
 *
 * With a contrast above 0 layers meet at their heights instead of cross fading, the larger the
 * contrast the sharper the border; 0 is the linear blend. The alpha is the weighted alpha of the
 * layers, which AlphaCutoff tests. 2D shapes ignore it.
 */
ECS_STRUCT(TextureBlend, {
    ecs_entity_t mask;      /**< Texture entity whose red, green, blue and alpha weigh layers
                             *   0..3, 0 for layer 0 alone */
    ecs_entity_t layers[4]; /**< Entities with PbrTextures and an optional TextureTransform, 0
                             *   for an unused layer */
    ecs_entity_t heights[4]; /**< Texture entity whose red is the height of each layer, stored,
                             *   0 for a flat height of 0.5; unused with a contrast of 0 */
    float contrast;         /**< Sharpness of the height based transitions, 0 (the default)
                             *   for the linear blend; not finite or negative is 0 */
});

/** How a TextureLayer changes one property of the surface beneath it where it covers it */
ECS_ENUM(TextureLayerBlend, {
    LayerKeep = 0,      /**< The property is left as it is */
    LayerMix = 1,       /**< The property is replaced by the value of the layer */
    LayerMultiply = 2   /**< The property is multiplied by the value of the layer */
});

/** Amount of a constructed TextureLayer: it covers everything its rules allow */
#define BOX_MATERIAL_LAYER_AMOUNT (1.0f)

/** Contrast of a constructed TextureLayer: a coverage edge over an eighth of the score */
#define BOX_MATERIAL_LAYER_CONTRAST (8.0f)

/** Layers a TextureLayers applies */
#define BOX_MATERIAL_TEXTURE_LAYERS (4)

/** A layer that paints over or modifies the resolved surface of the shapes whose TextureLayers
 * list its entity (snow, wetness, puddles, moss, frost, dust), default amount 1, direction up
 * (0, 1, 0), facing 0, no mask, cavity 0, contrast 8, albedo and roughness LayerMix, metallic
 * and occlusion LayerKeep, normal 0 and porosity 0.
 *
 * The layer entity carries its appearance with the material components: Rgb and PbrMaterial
 * as constants (white, roughness 1 and metallic 0 without them) times its PbrTextures (albedo,
 * ORM and normal, any texture entity, a TextureGraph output too), so a layer without textures is
 * a flat color and roughness. Its own TextureMapping places them, MappingWorld (the default of a
 * layer) so a layer runs on across shapes, with a triplanar blend above 0, or MappingUv for the
 * UVs of the mesh; MappingObject and MappingFrame project like MappingWorld. Its
 * TextureTransform and TextureScroll move them.
 *
 * Where it covers. Every fragment gets a coverage score from 0 to 1: how well its shading
 * normal faces direction (facing 0 ignores the orientation, a larger facing only allows normals
 * closer to direction, 1 only those that face it fully; the normal of the layer mixes it toward
 * the geometric normal, so a flattening water film fills the bumps), times the red of the mask raised by
 * cavity times how occluded the surface is (1 - its ORM red, or 1 - the height of a TextureBlend
 * with heights where that is lower), lowered for a negative cavity, so the crevices and low spots
 * come first without holding back open surfaces. amount moves a threshold over that
 * score: 0 covers nothing, 0.2 the best scoring places, 1 everything the rules allow. contrast is
 * the sharpness of the edge.
 *
 * What it changes. albedo, roughness, metallic and occlusion each keep, mix toward or multiply
 * by the value of the layer by the coverage; normal mixes the shading normal toward the normal
 * of the layer (the geometric normal for a flat layer, which flattens as a water film does).
 * With an altitude_fade above 0 the score is also scaled by how far the fragment is above
 * altitude in the absolute world (across shifts of the WorldOrigin), rising to 1 at altitude +
 * altitude_fade, so snow lies on high peaks only and its line moves down as the amount grows.
 *
 * porosity scales the change of the albedo and the roughness by how porous the surface beneath
 * is, (1 - metallic) times its roughness: at 1 a rough dielectric (soil, bark, stone) changes
 * fully, a glossy one little and a metal not at all, so one wet layer suits every material. The
 * alpha is never changed.
 *
 * The values are read every frame into a small buffer of the layer entity shared by every
 * batch that uses it, so writing amount (or any field) animates every shape with the layer
 * without moving them between batches. Only 3D meshes of a scene with Scene.texture_layers have
 * layers; Transmission and Distortion shapes, 2D shapes and texts ignore them. A Water shape
 * uses only their normals: they blend into its ripple normal by their coverage, before its
 * waves, the cavity ignored; their other channels do not change Water.
 */
ECS_STRUCT(TextureLayer, {
    float amount;           /**< Coverage, 0 none to 1 everything the rules allow */
    vec3 direction;         /**< World direction the covered surfaces face, normalized; zero
                             *   is up */
    float facing;           /**< 0 ignores the orientation, 1 only covers surfaces that face
                             *   direction fully; clamped to 0..1 */
    ecs_entity_t mask;      /**< Texture entity whose red is a coverage pattern, sampled with the
                             *   mapping of the layer, 0 for none */
    float cavity;           /**< -1..1: positive covers the occluded crevices first (puddles,
                             *   dust), negative the open ridges first (worn edges), 0 ignores
                             *   the occlusion */
    float contrast;         /**< Sharpness of the coverage edge, larger is harder; not positive
                             *   or not finite is the default */
    TextureLayerBlend albedo;    /**< How the albedo changes */
    TextureLayerBlend roughness; /**< How the roughness changes */
    TextureLayerBlend metallic;  /**< How the metallic changes */
    TextureLayerBlend occlusion; /**< How the occlusion changes */
    float normal;           /**< 0..1, how much the layer normal replaces the shading normal
                             *   where covered */
    float porosity;         /**< 0..1, how much the change of albedo and roughness follows the
                             *   porosity of the surface beneath */
    float altitude;         /**< Absolute world height in meters below which the layer covers
                             *   nothing, with altitude_fade above 0 */
    float altitude_fade;    /**< Meters above altitude over which the score rises from 0 to
                             *   1, so a smaller amount covers only the higher places; 0 (the
                             *   default) ignores the altitude */
});

/** Up to BOX_MATERIAL_TEXTURE_LAYERS TextureLayer entities applied in order over the resolved
 * surface of a 3D shape, after its PbrTextures, TextureBlend or TileTerrain and before lighting,
 * inherited by instances and part of the batch of a shape like PbrTextures.
 *
 * Shapes with the same layers share a batch; they draw with their own variants of the mesh
 * shaders. A shape without TextureLayers, or of a scene without Scene.texture_layers, draws with
 * the pipelines, bind groups and records it has without the feature; so does a Water shape
 * without TextureLayers, while one with them draws with the water layers variant. A layer
 * entity of 0, a deleted one or one without TextureLayer does nothing.
 */
ECS_STRUCT(TextureLayers, {
    ecs_entity_t layers[4]; /**< Entities with TextureLayer, 0 for an unused slot */
});

/** Material properties change every frame, applies to descendants */
extern ECS_TAG_DECLARE(DynamicMaterial);

/** Marks an entity as the frame of the projected textures of its subtree: a MappingFrame
 * material of the entity or of a descendant projects from the position relative to the nearest
 * one, so shapes under one frame show one continuous texture that moves with it */
extern ECS_TAG_DECLARE(TextureFrame);

/** Keeps an entity and its descendants out of the shadow pass of every view */
extern ECS_TAG_DECLARE(NoShadow);

/** Marks the fixture of a light, such as a lamp bulb or a lantern glass, applies to descendants.
 *
 * A 3D shape with it casts shadows as usual for the directional light and every spot and point
 * light, except into the tile of a light whose world position is inside the world bounds of the
 * shape: the local bounds of its mesh or surface moved by its model matrix, inclusive.
 *
 * The box is conservative: a large fixture also skips other lights inside its box. The shape
 * still receives light and shadows and is drawn as before. NoShadow wins, 2D shapes ignore it. */
extern ECS_TAG_DECLARE(LightFixture);

/** Register the box.material module.
 *
 * @param world The world
 */
void BoxMaterialImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
