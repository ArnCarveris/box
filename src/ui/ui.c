/**
 * @file ui/ui.c
 * @brief Registers the box.ui module, the defaults of a tree and its update.
 *
 * The update runs in immediate mode because it creates the view of a tree and
 * joins its nodes to a layer, which a deferred system cannot do in time for the
 * extract of the same frame. It shares the phase of the immediate script update
 * and runs next to it, so the pipeline gains no sync point of its own: one in
 * another phase would move the merge of the frame and change what every other
 * system sees, which the visual scenes catch.
 */

#define BOX_UI_IMPL
#include "ui_internal.h"

ECS_COMPONENT_DECLARE(UiImpl);
ECS_COMPONENT_DECLARE(UiNode);
ECS_COMPONENT_DECLARE(UiState);
ECS_TAG_DECLARE(UiGlyph);
ECS_TAG_DECLARE(UiShadow);
ECS_TAG_DECLARE(UiComposite);
ECS_TAG_DECLARE(UiQuad);
ECS_COMPONENT_DECLARE(ShadowImpl);
ECS_COMPONENT_DECLARE(GroupImpl);
ECS_COMPONENT_DECLARE(TextImpl);
ECS_COMPONENT_DECLARE(UiFonts);

/** Ui constructor: a tree that creates its own view at the UI priority.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_ui_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    Ui *ui = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        ui[i].view = 0;
        ui[i].height = 0;
        ui[i].scale = BOX_UI_SCALE;
        ui[i].priority = BOX_UI_PRIORITY;
    }
}

/** UiImpl constructor: a tree that is not bound yet and must be walked.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_ui_impl_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    UiImpl *impl = ptr;
    int32_t i;

    memset(ptr, 0, (size_t)count * sizeof(UiImpl));
    for (i = 0; i < count; i ++) {
        impl[i].dirty = true;
        impl[i].relayout = true;
    }
}

/** Registry destructor releasing the query and the scratch vectors.
 *
 * @param ptr The component
 * @param count Number of components
 * @param ti Type info
 */
static void box_ui_state_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    UiState *state = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_vec_fini_t(NULL, &state[i].roots, ecs_entity_t);
        ecs_vec_fini_t(NULL, &state[i].stack, UiVisit);
        ecs_vec_fini_t(NULL, &state[i].nodes, ecs_entity_t);
        ecs_vec_fini_t(NULL, &state[i].places, UiPlace);
        if (state[i].hits) {
            ecs_query_fini(state[i].hits);
        }
    }
}

/** Font registry destructor releasing the atlases and the scratch quads.
 *
 * @param ptr The component
 * @param count Number of components
 * @param ti Type info
 */
static void box_ui_fonts_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    UiFonts *fonts = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        box_ui_atlas_fini(&fonts[i]);
        ecs_vec_fini_t(NULL, &fonts[i].atlases, box_ui_atlas_t*);
        ecs_vec_fini_t(NULL, &fonts[i].quads, box_ui_quad_t);
    }
}

UiState* box_ui_state(
    ecs_world_t *world)
{
    UiState *state = ecs_get_mut(world, ecs_id(UiState), UiState);
    ecs_assert(state != NULL, ECS_INVALID_OPERATION, "box.ui is not imported");

    return state;
}

UiFonts* box_ui_fonts(
    ecs_world_t *world)
{
    UiFonts *fonts = ecs_get_mut(world, ecs_id(UiFonts), UiFonts);
    ecs_assert(fonts != NULL, ECS_INVALID_OPERATION, "box.ui is not imported");

    return fonts;
}

/** Ui on_set hook: rebind a tree that names another view and walk it again.
 *
 * @param it Iterator over the changed roots
 */
static void box_ui_on_set(
    ecs_iter_t *it)
{
    Ui *ui = ecs_field(it, Ui, 0);
    UiImpl *impl = ecs_field(it, UiImpl, 1);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (impl[i].view && ui[i].view && impl[i].view != ui[i].view) {
            box_ui_unbind(it->world, &impl[i]);
        }

        impl[i].dirty = true;
        impl[i].relayout = true;
    }
}

/** Ui on_remove hook: release the view and the state of the tree.
 *
 * @param it Iterator over the roots that lose their component
 */
static void box_ui_on_remove(
    ecs_iter_t *it)
{
    UiImpl *impl = ecs_field(it, UiImpl, 1);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_ui_unbind(it->stage, &impl[i]);
        ecs_remove(it->stage, it->entities[i], UiImpl);
    }
}

/** Mark every tree of the world, which an atlas that grew asks for.
 *
 * Growing an atlas moves every uv of the font it holds, so the elements that
 * drew with it have to build their quads again. Their fingerprint carries the
 * epoch of the atlas, so the next layout of a tree rebuilds the elements that
 * used the atlas and leaves every other element of the tree alone.
 *
 * @param world The world
 */
static void box_ui_regrow(
    ecs_world_t *world)
{
    UiState *state = box_ui_state(world);
    int32_t i, count = ecs_vec_count(&state->roots);
    for (i = 0; i < count; i ++) {
        ecs_entity_t root = ecs_vec_get_t(&state->roots, ecs_entity_t, i)[0];
        UiImpl *impl = ecs_get_mut(world, root, UiImpl);
        if (impl) {
            impl->relayout = true;
        }
    }

    box_ui_fonts(world)->grown = false;
}

void box_ui_update(
    ecs_world_t *world)
{
    UiState *state = box_ui_state(world);
    state->busy = true;
    ecs_vec_clear(&state->roots);
    ecs_iter_t it = ecs_each(world, Ui);
    while (ecs_each_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            ecs_vec_append_t(NULL, &state->roots, ecs_entity_t)[0] = it.entities[i];
        }
    }

    int32_t root;
    for (root = 0; root < ecs_vec_count(&state->roots); root ++) {
        ecs_entity_t e = ecs_vec_get_t(&state->roots, ecs_entity_t, root)[0];
        Ui *ui = ecs_get_mut(world, e, Ui);
        UiImpl *impl = ecs_get_mut(world, e, UiImpl);
        if (!ui || !impl) {
            continue;
        }

        box_ui_bind(world, ui, impl);
        box_ui_sync(world, ui, impl);
        if (impl->dirty || impl->relayout) {
            box_ui_walk(world, e);
        }
    }

    if (box_ui_fonts(world)->grown) {
        box_ui_regrow(world);
    }

    state = box_ui_state(world);
    state->busy = false;
}

/** Update system: bind, size and number every tree of the world.
 *
 * @param it System iterator
 */
static void BoxUiUpdate(
    ecs_iter_t *it)
{
    box_ui_update(it->world);
}

void BoxUiImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxUi);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxCamera);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxRender);
    ECS_IMPORT(world, BoxGeometry);
    ECS_IMPORT(world, BoxMaterial);
    ECS_IMPORT(world, BoxTexture);
    ECS_IMPORT(world, BoxPicking);

    ECS_META_COMPONENT(world, Align);
    ECS_META_COMPONENT(world, VAlign);
    ECS_META_COMPONENT(world, Direction);
    ECS_META_COMPONENT(world, Ui);
    ECS_META_COMPONENT(world, Layout);
    ECS_META_COMPONENT(world, Anchor);
    ECS_META_COMPONENT(world, Bounds);
    ECS_META_COMPONENT(world, BoxShadow);
    ECS_META_COMPONENT(world, Font);
    ECS_META_COMPONENT(world, Text);
    ECS_COMPONENT_DEFINE(world, UiImpl);
    ECS_COMPONENT_DEFINE(world, UiNode);
    ECS_COMPONENT_DEFINE(world, UiState);
    ECS_TAG_DEFINE(world, UiGlyph);
    ECS_TAG_DEFINE(world, UiShadow);
    ECS_TAG_DEFINE(world, UiComposite);
    ECS_TAG_DEFINE(world, UiQuad);
    ECS_COMPONENT_DEFINE(world, ShadowImpl);
    ECS_COMPONENT_DEFINE(world, GroupImpl);
    ECS_COMPONENT_DEFINE(world, TextImpl);
    ECS_COMPONENT_DEFINE(world, UiFonts);

    ecs_set_hooks(world, Ui, { .ctor = box_ui_ctor });
    ecs_set_hooks(world, UiImpl, { .ctor = box_ui_impl_ctor });
    ecs_set_hooks(world, UiNode, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, Layout, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, Anchor, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, Bounds, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, BoxShadow, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, ShadowImpl, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, GroupImpl, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, UiState, {
        .ctor = flecs_default_ctor,
        .dtor = box_ui_state_dtor
    });
    ecs_set_hooks(world, Font, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, TextImpl, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, UiFonts, {
        .ctor = flecs_default_ctor,
        .dtor = box_ui_fonts_dtor
    });

    ecs_add_pair(world, ecs_id(Ui), EcsWith, ecs_id(UiImpl));
    ecs_add_pair(world, ecs_id(Ui), EcsWith, EcsOrderedChildren);
    ecs_add_pair(world, ecs_id(UiImpl), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(UiNode), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(Bounds), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(Layout), EcsWith, EcsOrderedChildren);
    ecs_add_pair(world, ecs_id(Font), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(Text), EcsWith, ecs_id(TextImpl));
    ecs_add_pair(world, ecs_id(TextImpl), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(BoxShadow), EcsWith, ecs_id(ShadowImpl));
    ecs_add_pair(world, ecs_id(ShadowImpl), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(GroupImpl), EcsOnInstantiate, EcsDontInherit);

    UiState *state = ecs_singleton_ensure(world, UiState);
    ecs_vec_init_t(NULL, &state->roots, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &state->stack, UiVisit, 0);
    ecs_vec_init_t(NULL, &state->nodes, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &state->places, UiPlace, 0);

    UiFonts *fonts = ecs_singleton_ensure(world, UiFonts);
    ecs_vec_init_t(NULL, &fonts->atlases, box_ui_atlas_t*, 0);
    ecs_vec_init_t(NULL, &fonts->quads, box_ui_quad_t, 0);

    ecs_observer(world, {
        .query.terms = {
            { .id = ecs_id(Ui), .inout = EcsIn },
            { .id = ecs_id(UiImpl), .inout = EcsInOut }
        },
        .events = { EcsOnSet },
        .callback = box_ui_on_set
    });

    ecs_observer(world, {
        .query.terms = {
            { .id = ecs_id(Ui), .inout = EcsInOutNone },
            { .id = ecs_id(UiImpl), .inout = EcsInOut }
        },
        .events = { EcsOnRemove },
        .callback = box_ui_on_remove
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxUiUpdate" }),
        .phase = EcsPreUpdate,
        .query.terms = {{ .id = ecs_id(UiState) }},
        .callback = BoxUiUpdate,
        .immediate = true
    });

    box_ui_tree_init(world);
    box_ui_text_init(world);
    box_ui_shadow_init(world);
    box_ui_group_init(world);
    box_ui_hit_init(world);
    box_ui_functions_init(world, ecs_id(BoxUi));
}
