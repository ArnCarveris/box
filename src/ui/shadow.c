/**
 * @file ui/shadow.c
 * @brief Draws the box shadow of an element as a soft quad behind it.
 *
 * The shadow is a child quad covering the box grown by spread and blur; the fragment
 * stage fades the rounded rectangle out over the blur. Its near black metal material
 * carries the shadow color in emission, so lighting does not change it.
 */

#include "ui_internal.h"

/** Prefab the shadow quads of a world take their material from.
 *
 * @param world The world
 * @return The prefab
 */
static ecs_entity_t box_ui_shadow_material(
    ecs_world_t *world)
{
    UiState *state = box_ui_state(world);
    ecs_entity_t result = state->shadow_prefab;
    if (result && ecs_is_alive(world, result)) {
        goto done;
    }

    result = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, result, BlendMode, { AlphaBlend });
    ecs_set(world, result, PbrMaterial, { .metallic = 1, .roughness = 0 });
    ecs_set(world, result, Rgb, { 1, 1, 1 });
    box_ui_state(world)->shadow_prefab = result;
done:
    return result;
}

/** Corner radii of the rounded rectangle an element draws, zero without one.
 *
 * @param world The world
 * @param e The element
 * @param out Receives the four radii in logical pixels
 */
static void box_ui_shadow_radii(
    ecs_world_t *world,
    ecs_entity_t e,
    float *out)
{
    const RenderShape2D *shape = ecs_get(world, e, RenderShape2D);
    int32_t i;
    for (i = 0; i < BOX_SHAPE2D_CORNERS; i ++) {
        out[i] = shape ? shape->radii[i] : 0.0f;
    }
}

/** Fingerprint of everything a build of one shadow quad depends on.
 *
 * @param p The place of the element
 * @param shadow The shadow of the element
 * @return The fingerprint
 */
static uint64_t box_ui_shadow_print(
    const UiPlace *p,
    const BoxShadow *shadow)
{
    uint64_t hash = 14695981039346656037u;
    float values[] = { p->width, p->height, shadow->x, shadow->y, shadow->blur,
        shadow->spread };
    uint32_t color = (uint32_t)shadow->color.r | ((uint32_t)shadow->color.g << 8) |
        ((uint32_t)shadow->color.b << 16) | ((uint32_t)shadow->color.a << 24);

    int32_t i;
    for (i = 0; i < 6; i ++) {
        uint32_t bits;

        memcpy(&bits, &values[i], sizeof(bits));
        hash = (hash ^ bits) * 1099511628211u;
    }

    return (hash ^ color) * 1099511628211u;
}

/** Give the quad of a shadow the rectangle, the shape and the color it draws.
 *
 * @param world The world
 * @param quad The quad
 * @param p The place of the element
 * @param shadow The shadow of the element
 */
static void box_ui_shadow_quad(
    ecs_world_t *world,
    ecs_entity_t quad,
    const UiPlace *p,
    const BoxShadow *shadow)
{
    float grow = shadow->spread + shadow->blur;
    float width = p->width + grow * 2.0f;
    float height = p->height + grow * 2.0f;
    float radii[BOX_SHAPE2D_CORNERS];
    RenderShape2D shape;

    memset(&shape, 0, sizeof(shape));

    width = width > 0 ? width : 0;
    height = height > 0 ? height : 0;
    box_ui_shadow_radii(world, p->e, radii);

    int32_t i;
    for (i = 0; i < BOX_SHAPE2D_CORNERS; i ++) {
        float radius = radii[i] + grow;

        shape.radii[i] = radius > 0 ? radius : 0;
    }

    for (i = 0; i < BOX_SHAPE2D_EDGES; i ++) {
        shape.widths[i] = shadow->blur > 0 ? shadow->blur : 0;
    }

    ecs_set(world, quad, Position, { shadow->x, -shadow->y, 0 });
    ecs_set(world, quad, Opacity, { (float)shadow->color.a / 255.0f });
    ecs_set(world, quad, Emissive, { .strength = 1.0f, .color = {
        shadow->color.r, shadow->color.g, shadow->color.b } });
    ecs_set_ptr(world, quad, RenderShape2D, &shape);
    ecs_set(world, quad, Rect, { width, height });
}

/** Drop the quad an element no longer needs.
 *
 * @param world The world
 * @param impl The shadow state of the element
 */
static void box_ui_shadow_clear(
    ecs_world_t *world,
    ShadowImpl *impl)
{
    if (!impl->quad) {
        goto done;
    }

    if (ecs_is_alive(world, impl->quad)) {
        ecs_delete(world, impl->quad);
        box_ui_state(world)->respawn = true;
    }

    impl->quad = 0;
    impl->hash = 0;
done:
    return;
}

/** Draw or drop the shadow of one element of the collected tree.
 *
 * @param world The world
 * @param p The place of the element
 */
static void box_ui_shadow_element(
    ecs_world_t *world,
    const UiPlace *p)
{
    const BoxShadow *shadow = ecs_get(world, p->e, BoxShadow);
    ShadowImpl *impl = ecs_get_mut(world, p->e, ShadowImpl);
    if (!impl) {
        goto done;
    }

    if (!shadow || !shadow->color.a || p->width <= 0 || p->height <= 0) {
        box_ui_shadow_clear(world, impl);
        goto done;
    }

    uint64_t hash = box_ui_shadow_print(p, shadow);
    if (impl->quad && ecs_is_alive(world, impl->quad) && impl->hash == hash) {
        goto done;
    }

    if (!impl->quad || !ecs_is_alive(world, impl->quad)) {
        ecs_entity_t quad = ecs_new_w_pair(world, EcsChildOf, p->e);

        ecs_add_id(world, quad, UiShadow);
        ecs_add_id(world, quad, UiQuad);
        ecs_add_id(world, quad, RenderSoftShape);
        ecs_add_pair(world, quad, EcsIsA, box_ui_shadow_material(world));
        impl = ecs_get_mut(world, p->e, ShadowImpl);
        impl->quad = quad;
        box_ui_state(world)->respawn = true;
    }

    box_ui_shadow_quad(world, impl->quad, p, shadow);
    impl = ecs_get_mut(world, p->e, ShadowImpl);
    impl->hash = hash;
done:
    return;
}

void box_ui_shadow_build(
    ecs_world_t *world)
{
    UiState *state = box_ui_state(world);
    if (!state->shadows) {
        goto done;
    }

    int32_t i, count = ecs_vec_count(&state->places);
    for (i = 0; i < count; i ++) {
        UiPlace place = ecs_vec_get_t(&box_ui_state(world)->places, UiPlace, i)[0];
        if (place.aux || !place.has_shadow) {
            continue;
        }

        box_ui_shadow_element(world, &place);
    }

done:
    return;
}

/** BoxShadow observer: count the shadows so a world without one pays none.
 *
 * @param it Iterator over the elements that gained or lost a shadow
 */
static void box_ui_on_shadow(
    ecs_iter_t *it)
{
    UiState *state = box_ui_state(it->world);

    state->shadows += it->event == EcsOnAdd ? it->count : -it->count;
}

/** BoxShadow observer: an element that loses its shadow loses its state as well.
 *
 * @param it Iterator over the elements that lose the component
 */
static void box_ui_shadow_dropped(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_remove(it->stage, it->entities[i], ShadowImpl);
    }
}

/** ShadowImpl destructor: the quad goes with the element that cast it.
 *
 * @param it Iterator over the elements that lose their shadow
 */
static void box_ui_shadow_on_remove(
    ecs_iter_t *it)
{
    ShadowImpl *impl = ecs_field(it, ShadowImpl, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (impl[i].quad && ecs_is_alive(it->world, impl[i].quad)) {
            ecs_delete(it->stage, impl[i].quad);
        }

        impl[i].quad = 0;
    }
}

void box_ui_shadow_init(
    ecs_world_t *world)
{
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(BoxShadow), .inout = EcsInOutNone }},
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = box_ui_on_shadow
    });

    ecs_observer(world, {
        .query.terms = {
            { .id = ecs_id(BoxShadow), .inout = EcsInOutNone },
            { .id = ecs_id(ShadowImpl), .inout = EcsInOutFilter }
        },
        .events = { EcsOnRemove },
        .callback = box_ui_shadow_dropped
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(ShadowImpl), .inout = EcsInOut }},
        .events = { EcsOnRemove },
        .callback = box_ui_shadow_on_remove
    });
}
