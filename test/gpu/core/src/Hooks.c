/**
 * @file Hooks.c
 * @brief Tests that reflected components with string or vector members copy, free and move them.
 */

#include <box.h>
#include <bake_test.h>

static ecs_os_api_free_t hooks_free_prev;
static const void *hooks_watch;
static int32_t hooks_freed;

static void hooks_free(
    void *ptr)
{
    if (ptr && ptr == hooks_watch) {
        hooks_freed ++;
    }

    hooks_free_prev(ptr);
}

void Hooks_app(void) {
    // engine_core/meta_hooks
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    hooks_free_prev = ecs_os_api.free_;
    ecs_os_api.free_ = hooks_free;

    const char *literal = "scenes/none.flecs";
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, App, { .scene = ECS_CONST_CAST(char*, literal) });
    char *orig = ecs_get(world, e, App)->scene;
    test_assert(orig != literal);
    test_str(orig, literal);

    ecs_entity_t c = ecs_new(world);
    ecs_set_ptr(world, c, App, ecs_get(world, e, App));
    char *copy = ecs_get(world, c, App)->scene;
    test_assert(copy != orig);
    test_str(copy, literal);
    test_assert(ecs_get(world, e, App)->scene == orig);

    hooks_watch = copy;
    hooks_freed = 0;
    ecs_remove(world, c, App);
    test_int(hooks_freed, 1);
    test_str(ecs_get(world, e, App)->scene, literal);

    hooks_watch = orig;
    hooks_freed = 0;
    ecs_delete(world, e);
    test_int(hooks_freed, 1);

    hooks_watch = NULL;
    ecs_os_api.free_ = hooks_free_prev;
    ecs_fini(world);
}

void Hooks_app_status(void) {
    // engine_core/meta_hooks
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    hooks_free_prev = ecs_os_api.free_;
    ecs_os_api.free_ = hooks_free;

    const char *literal = "/tmp/box/none";
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, AppStatus, { .tmp_dir = ECS_CONST_CAST(char*, literal) });
    char *orig = ecs_get(world, e, AppStatus)->tmp_dir;
    test_assert(orig != literal);
    test_str(orig, literal);

    ecs_entity_t c = ecs_new(world);
    ecs_set_ptr(world, c, AppStatus, ecs_get(world, e, AppStatus));
    char *copy = ecs_get(world, c, AppStatus)->tmp_dir;
    test_assert(copy != orig);
    test_str(copy, literal);
    test_assert(ecs_get(world, e, AppStatus)->tmp_dir == orig);

    hooks_watch = copy;
    hooks_freed = 0;
    ecs_remove(world, c, AppStatus);
    test_int(hooks_freed, 1);
    test_str(ecs_get(world, e, AppStatus)->tmp_dir, literal);

    hooks_watch = orig;
    hooks_freed = 0;
    ecs_delete(world, e);
    test_int(hooks_freed, 1);

    hooks_watch = NULL;
    ecs_os_api.free_ = hooks_free_prev;
    ecs_fini(world);
}

void Hooks_action(void) {
    // engine_core/meta_hooks
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    hooks_free_prev = ecs_os_api.free_;
    ecs_os_api.free_ = hooks_free;

    const char *literal = "Jump over things";
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Action, { .description = ECS_CONST_CAST(char*, literal) });
    char *orig = ecs_get(world, e, Action)->description;
    test_assert(orig != literal);
    test_str(orig, literal);

    ecs_entity_t c = ecs_new(world);
    ecs_set_ptr(world, c, Action, ecs_get(world, e, Action));
    char *copy = ecs_get(world, c, Action)->description;
    test_assert(copy != orig);
    test_str(copy, literal);
    test_assert(ecs_get(world, e, Action)->description == orig);

    hooks_watch = copy;
    hooks_freed = 0;
    ecs_remove(world, c, Action);
    test_int(hooks_freed, 1);
    test_str(ecs_get(world, e, Action)->description, literal);

    hooks_watch = orig;
    hooks_freed = 0;
    ecs_delete(world, e);
    test_int(hooks_freed, 1);

    hooks_watch = NULL;
    ecs_os_api.free_ = hooks_free_prev;
    ecs_fini(world);
}

void Hooks_mesh3(void) {
    // engine_core/meta_hooks
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    hooks_free_prev = ecs_os_api.free_;
    ecs_os_api.free_ = hooks_free;

    ecs_entity_t e = ecs_new(world);
    Mesh3 *m = ecs_ensure(world, e, Mesh3);
    ecs_vec_init_t(NULL, &m->vertices, vec3, 3);
    ecs_vec_set_count_t(NULL, &m->vertices, vec3, 3);
    vec3 *p = ecs_vec_first_t(&m->vertices, vec3);
    glm_vec3_copy((vec3){ 0.0f, 0.0f, 0.0f }, p[0]);
    glm_vec3_copy((vec3){ 1.0f, 0.0f, 0.0f }, p[1]);
    glm_vec3_copy((vec3){ 0.0f, 1.0f, 0.0f }, p[2]);
    ecs_vec_init_t(NULL, &m->indices, uint32_t, 3);
    ecs_vec_set_count_t(NULL, &m->indices, uint32_t, 3);
    uint32_t *idx = ecs_vec_first_t(&m->indices, uint32_t);
    idx[0] = 0;
    idx[1] = 1;
    idx[2] = 2;
    ecs_modified(world, e, Mesh3);
    void *orig = ecs_get(world, e, Mesh3)->vertices.array;
    void *orig_idx = ecs_get(world, e, Mesh3)->indices.array;

    ecs_entity_t c = ecs_new(world);
    ecs_set_ptr(world, c, Mesh3, ecs_get(world, e, Mesh3));
    const Mesh3 *copy = ecs_get(world, c, Mesh3);
    test_assert(copy->vertices.array != orig);
    test_assert(copy->indices.array != orig_idx);
    test_int(ecs_vec_count(&copy->vertices), 3);
    test_int(ecs_vec_count(&copy->indices), 3);
    test_int(ecs_vec_count(&copy->normals), 0);
    test_flt(ecs_vec_get_t(&copy->vertices, vec3, 1)[0][0], 1.0f);
    test_int(ecs_vec_get_t(&copy->indices, uint32_t, 2)[0], 2);
    test_assert(ecs_get(world, e, Mesh3)->vertices.array == orig);

    hooks_watch = copy->vertices.array;
    hooks_freed = 0;
    ecs_remove(world, c, Mesh3);
    test_int(hooks_freed, 1);
    test_flt(ecs_vec_get_t(&ecs_get(world, e, Mesh3)->vertices, vec3, 2)[0][1], 1.0f);

    hooks_watch = orig_idx;
    hooks_freed = 0;
    ecs_delete(world, e);
    test_int(hooks_freed, 1);

    hooks_watch = NULL;
    ecs_os_api.free_ = hooks_free_prev;
    ecs_fini(world);
}

void Hooks_terminal(void) {
    // engine_core/meta_hooks
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    hooks_free_prev = ecs_os_api.free_;
    ecs_os_api.free_ = hooks_free;

    const char *literal = "flecs.core";
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Terminal, { .cwd = ECS_CONST_CAST(char*, literal) });
    char *orig = ecs_get(world, e, Terminal)->cwd;
    test_assert(orig != literal);
    test_str(orig, literal);

    ecs_entity_t c = ecs_new(world);
    ecs_set_ptr(world, c, Terminal, ecs_get(world, e, Terminal));
    char *copy = ecs_get(world, c, Terminal)->cwd;
    test_assert(copy != orig);
    test_str(copy, literal);
    test_assert(ecs_get(world, e, Terminal)->cwd == orig);

    hooks_watch = copy;
    hooks_freed = 0;
    ecs_remove(world, c, Terminal);
    test_int(hooks_freed, 1);
    test_str(ecs_get(world, e, Terminal)->cwd, literal);

    hooks_watch = orig;
    hooks_freed = 0;
    ecs_delete(world, e);
    test_int(hooks_freed, 1);

    hooks_watch = NULL;
    ecs_os_api.free_ = hooks_free_prev;
    ecs_fini(world);
}

void Hooks_terminal_status(void) {
    // engine_core/meta_hooks
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    hooks_free_prev = ecs_os_api.free_;
    ecs_os_api.free_ = hooks_free;

    const char *literal = "/tmp/box/none/session";
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, TerminalStatus, { .dir = ECS_CONST_CAST(char*, literal) });
    char *orig = ecs_get(world, e, TerminalStatus)->dir;
    test_assert(orig != literal);
    test_str(orig, literal);

    ecs_entity_t c = ecs_new(world);
    ecs_set_ptr(world, c, TerminalStatus, ecs_get(world, e, TerminalStatus));
    char *copy = ecs_get(world, c, TerminalStatus)->dir;
    test_assert(copy != orig);
    test_str(copy, literal);
    test_assert(ecs_get(world, e, TerminalStatus)->dir == orig);

    hooks_watch = copy;
    hooks_freed = 0;
    ecs_remove(world, c, TerminalStatus);
    test_int(hooks_freed, 1);
    test_str(ecs_get(world, e, TerminalStatus)->dir, literal);

    hooks_watch = orig;
    hooks_freed = 0;
    ecs_delete(world, e);
    test_int(hooks_freed, 1);

    hooks_watch = NULL;
    ecs_os_api.free_ = hooks_free_prev;
    ecs_fini(world);
}

void Hooks_texture_path(void) {
    // engine_core/meta_hooks
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ECS_IMPORT(world, BoxTexture);
    hooks_free_prev = ecs_os_api.free_;
    ecs_os_api.free_ = hooks_free;

    const char *literal = "textures/none.png";
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, TexturePath, { .path = ECS_CONST_CAST(char*, literal) });
    char *orig = ecs_get(world, e, TexturePath)->path;
    test_assert(orig != literal);
    test_str(orig, literal);

    ecs_entity_t c = ecs_new(world);
    ecs_set_ptr(world, c, TexturePath, ecs_get(world, e, TexturePath));
    char *copy = ecs_get(world, c, TexturePath)->path;
    test_assert(copy != orig);
    test_str(copy, literal);
    test_assert(ecs_get(world, e, TexturePath)->path == orig);

    hooks_watch = copy;
    hooks_freed = 0;
    ecs_remove(world, c, TexturePath);
    test_int(hooks_freed, 1);
    test_str(ecs_get(world, e, TexturePath)->path, literal);

    hooks_watch = orig;
    hooks_freed = 0;
    ecs_delete(world, e);
    test_int(hooks_freed, 1);

    hooks_watch = NULL;
    ecs_os_api.free_ = hooks_free_prev;
    ecs_fini(world);
}

void Hooks_surface(void) {
    // engine_core/meta_hooks
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    hooks_free_prev = ecs_os_api.free_;
    ecs_os_api.free_ = hooks_free;

    const char *literal = "none.png";
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, { .write_to_file = ECS_CONST_CAST(char*, literal),
        .resolution_scale = 1 });
    char *orig = ecs_get(world, e, Surface)->write_to_file;
    test_assert(orig != literal);
    test_str(orig, literal);

    ecs_entity_t c = ecs_new(world);
    ecs_set_ptr(world, c, Surface, ecs_get(world, e, Surface));
    char *copy = ecs_get(world, c, Surface)->write_to_file;
    test_assert(copy != orig);
    test_str(copy, literal);
    test_assert(ecs_get(world, e, Surface)->write_to_file == orig);

    hooks_watch = copy;
    hooks_freed = 0;
    ecs_remove(world, c, Surface);
    test_int(hooks_freed, 1);
    test_str(ecs_get(world, e, Surface)->write_to_file, literal);

    hooks_watch = orig;
    hooks_freed = 0;
    ecs_delete(world, e);
    test_int(hooks_freed, 1);

    hooks_watch = NULL;
    ecs_os_api.free_ = hooks_free_prev;
    ecs_fini(world);
}

void Hooks_view(void) {
    // engine_core/meta_hooks
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    hooks_free_prev = ecs_os_api.free_;
    ecs_os_api.free_ = hooks_free;

    ecs_entity_t effect = ecs_new(world);
    ecs_entity_t e = ecs_new(world);
    View *v = ecs_ensure(world, e, View);
    ecs_vec_init_t(NULL, &v->effects, ViewEffect, 1);
    *ecs_vec_append_t(NULL, &v->effects, ViewEffect) = (ViewEffect){
        .enabled = true, .effect = effect
    };
    ecs_modified(world, e, View);
    void *orig = ecs_get(world, e, View)->effects.array;

    ecs_entity_t c = ecs_new(world);
    ecs_set_ptr(world, c, View, ecs_get(world, e, View));
    const View *copy = ecs_get(world, c, View);
    test_assert(copy->effects.array != orig);
    test_int(ecs_vec_count(&copy->effects), 1);
    test_uint(ecs_vec_first_t(&copy->effects, ViewEffect)->effect, effect);
    test_assert(ecs_get(world, e, View)->effects.array == orig);

    hooks_watch = copy->effects.array;
    hooks_freed = 0;
    ecs_remove(world, c, View);
    test_int(hooks_freed, 1);
    test_uint(ecs_vec_first_t(&ecs_get(world, e, View)->effects, ViewEffect)->effect, effect);

    hooks_watch = orig;
    hooks_freed = 0;
    ecs_delete(world, e);
    test_int(hooks_freed, 1);

    hooks_watch = NULL;
    ecs_os_api.free_ = hooks_free_prev;
    ecs_fini(world);
}

void Hooks_owning_components_have_hooks(void) {
    // engine_core/meta_hooks
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ECS_IMPORT(world, BoxTexture);

    ecs_entity_t box = ecs_lookup(world, "box");
    test_assert(box != 0);

    int32_t owning = 0;
    ecs_iter_t it = ecs_each(world, EcsStruct);
    while (ecs_each_next(&it)) {
        const EcsStruct *st = ecs_field(&it, EcsStruct, 0);
        for (int32_t i = 0; i < it.count; i ++) {
            ecs_entity_t parent = ecs_get_parent(world, it.entities[i]);
            while (parent && parent != box) {
                parent = ecs_get_parent(world, parent);
            }

            if (!parent) {
                continue;
            }

            const ecs_type_info_t *ti = ecs_get_type_info(world, it.entities[i]);
            test_assert(ti != NULL);
            const ecs_member_t *members = st[i].members.array;
            bool owns = false;
            for (int32_t m = 0; m < st[i].members.count; m ++) {
                const ecs_type_info_t *mti = ecs_get_type_info(world, members[m].type);
                const EcsVector *vec = ecs_get(world, members[m].type, EcsVector);
                if (vec) {
                    const ecs_type_info_t *eti = ecs_get_type_info(world, vec->type);
                    test_assert(eti != NULL);
                    if (eti->hooks.dtor) {
                        char *path = ecs_get_path(world, it.entities[i]);
                        test_str(path, NULL);
                    }
                }

                owns |= mti && mti->hooks.dtor;
            }

            if (owns) {
                char *path = ecs_get_path(world, it.entities[i]);
                if (!ti->hooks.dtor || !ti->hooks.copy || !ti->hooks.move) {
                    test_str(path, NULL);
                }

                ecs_os_free(path);
                owning ++;
            }
        }
    }

    test_assert(owning > 0);

    ecs_fini(world);
}
