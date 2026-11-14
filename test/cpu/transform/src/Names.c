/**
 * @file Names.c
 * @brief Names of the systems, observers and queries of box.transform.
 */

#include <box.h>
#include <bake_test.h>

/** Test whether a path names an entity with a poly of a kind.
 *
 * @param world The world
 * @param path The path of the entity
 * @param kind EcsSystem, EcsObserver or EcsQuery
 * @return Whether the entity exists and has the poly
 */
static bool names_has(
    ecs_world_t *world,
    const char *path,
    ecs_entity_t kind)
{
    ecs_entity_t e = ecs_lookup(world, path);
    return e != 0 && ecs_has_pair(world, e, ecs_id(EcsPoly), kind);
}

void Names_static_names(void) {
    // transform/names
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t ns = ecs_lookup(world, "box.transform.static");
    test_assert(ns != 0);
    test_assert(ecs_has_id(world, ns, EcsModule));
    test_assert(names_has(world, "box.transform.static.Update", EcsSystem));
    test_assert(names_has(world, "box.transform.static.OnPosition", EcsObserver));
    test_assert(names_has(world, "box.transform.static.OnRotation", EcsObserver));
    test_assert(names_has(world, "box.transform.static.OnScale", EcsObserver));
    test_assert(names_has(world, "box.transform.static.OnManual", EcsObserver));
    test_assert(names_has(world, "box.transform.static.OnChildOf", EcsObserver));
    test_assert(names_has(world, "box.transform.static.OnParent", EcsObserver));
    test_assert(names_has(world, "box.transform.static.OnEnable", EcsObserver));
    test_assert(ecs_lookup(world, "box.transform.TransformStatic") == 0);
    test_assert(ecs_lookup(world, "box.transform.TransformStatic_OnScale") == 0);

    ecs_fini(world);
}

void Names_dynamic_names(void) {
    // transform/names
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    ecs_entity_t ns = ecs_lookup(world, "box.transform.dynamic");
    test_assert(ns != 0);
    test_assert(ecs_has_id(world, ns, EcsModule));
    test_assert(names_has(world, "box.transform.dynamic.Update", EcsSystem));
    test_assert(names_has(world, "box.transform.dynamic.Spread", EcsObserver));
    test_assert(names_has(world, "box.transform.dynamic.SpreadToParented", EcsObserver));
    test_assert(names_has(world, "box.transform.dynamic.ChildOfQuery", EcsQuery));
    test_assert(names_has(world, "box.transform.dynamic.ParentQuery", EcsQuery));
    test_assert(ecs_lookup(world, "box.transform.TransformDynamic") == 0);
    test_assert(ecs_lookup(world, "box.transform.TransformDynamic_Spread") == 0);

    ecs_fini(world);
}

void Names_module_names(void) {
    // transform/names
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxTransform);

    test_assert(names_has(world, "box.transform.OnDynamic", EcsObserver));
    test_assert(names_has(world, "box.transform.OnRemovePosition", EcsObserver));
    test_assert(ecs_lookup(world, "box.transform.Transform_OnDynamic") == 0);
    test_uint(ecs_lookup(world, "box.transform.Position"), ecs_id(Position));

    ecs_fini(world);
}
