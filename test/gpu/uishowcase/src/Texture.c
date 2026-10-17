/**
 * @file Texture.c
 * @brief The render-to-texture UI of the UI showcase: a curved screen of slices that sample the
 * texture of a UI scene, picked through the UV of the slice under the cursor.
 */

#include "showcase.h"
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>

/** Window points of the START button on the left end of the screen */
#define TEXTURE_START "[760, 250]"

/** Window points of the STOP button on the right end of the screen */
#define TEXTURE_STOP "[975, 250]"

/** Window points of the screen between the two buttons */
#define TEXTURE_FRAME "[865, 250]"

/** Path of the START button in the UI scene of the texture */
#define TEXTURE_START_PATH "showcase.screen_ui.dashboard.controls.start"

/** Path of the STOP button in the UI scene of the texture */
#define TEXTURE_STOP_PATH "showcase.screen_ui.dashboard.controls.stop"

/** Path of the status text in the UI scene of the texture */
#define TEXTURE_STATUS_PATH "showcase.screen_ui.dashboard.head.status"

/** Check the state of the reactor, its status text and the glow of its core.
 *
 * @param world The world
 * @param session The terminal session
 * @param on Whether the reactor runs
 */
static void texture_expect_reactor(
    ecs_world_t *world,
    ecs_entity_t session,
    bool on)
{
    showcase_expect(world, session, "get showcase Showcase.mut.reactor", on ? "true" : "false");
    showcase_expect(world, session, "get " TEXTURE_STATUS_PATH " Text.value",
        on ? "\"ONLINE\"" : "\"OFFLINE\"");
    test_bool(ecs_has(world, ecs_lookup(world, "showcase.core"), Emissive), on);
}

/** Get the slice of the curved screen the last pick hit.
 *
 * @param world The world
 * @param session The terminal session
 * @return The path of the slice, owned by the caller
 */
static char* texture_hit_slice(
    ecs_world_t *world,
    ecs_entity_t session)
{
    char *result = showcase_run(world, session, "get box.picking.PickStatus PickStatus.hit");
    test_assert(!strncmp(result, "showcase.screen.slice_", strlen("showcase.screen.slice_")));

    return result;
}

void Texture_hover_start_button(void) {
    // uishowcase/texture
    ecs_entity_t s = 0;
    ecs_world_t *world = showcase_load(&s);

    showcase_hover(world, s, TEXTURE_START, TEXTURE_START_PATH);
    char *target = showcase_run(world, s, "get box.picking.PickStatus PickStatus.target");
    test_assert(!strncmp(target, "showcase.screen_ui.", strlen("showcase.screen_ui.")));
    ecs_os_free(target);
    showcase_expect(world, s, "get box.picking.PickStatus PickStatus.view",
        "showcase.screen_views.view");
    showcase_expect(world, s, "get box.picking.PickStatus PickStatus.hit_view",
        "showcase.world_view");
    ecs_os_free(texture_hit_slice(world, s));
    showcase_expect(world, s, "get " TEXTURE_START_PATH " Button.mut.hover", "true");
    showcase_expect(world, s, "get " TEXTURE_START_PATH " Rgb", "{r: 90, g: 140, b: 220}");

    showcase_hover(world, s, TEXTURE_FRAME, "#0");
    showcase_expect(world, s, "get " TEXTURE_START_PATH " Button.mut.hover", "false");
    showcase_expect(world, s, "get " TEXTURE_START_PATH " Rgb", "{r: 60, g: 100, b: 170}");

    ecs_fini(world);
}

void Texture_start_through_uv(void) {
    // uishowcase/texture
    ecs_entity_t s = 0;
    ecs_world_t *world = showcase_load(&s);
    texture_expect_reactor(world, s, false);

    showcase_hover(world, s, TEXTURE_START, TEXTURE_START_PATH);
    showcase_click(world, s, TEXTURE_START);
    texture_expect_reactor(world, s, true);
    showcase_expect(world, s, "get showcase Showcase.mut.last", "\"reactor_start\"");

    showcase_hover(world, s, TEXTURE_START, TEXTURE_START_PATH);
    showcase_click(world, s, TEXTURE_START);
    texture_expect_reactor(world, s, true);

    ecs_fini(world);
}

void Texture_stop_on_other_slice(void) {
    // uishowcase/texture
    ecs_entity_t s = 0;
    ecs_world_t *world = showcase_load(&s);

    showcase_hover(world, s, TEXTURE_START, TEXTURE_START_PATH);
    char *start = texture_hit_slice(world, s);
    showcase_click(world, s, TEXTURE_START);
    texture_expect_reactor(world, s, true);

    showcase_hover(world, s, TEXTURE_STOP, TEXTURE_STOP_PATH);
    char *stop = texture_hit_slice(world, s);
    test_assert(strcmp(start, stop) != 0);
    char *start_offset = showcase_run(world, s, "get %s TextureTransform.offset_x", start);
    char *stop_offset = showcase_run(world, s, "get %s TextureTransform.offset_x", stop);
    test_assert(strtod(start_offset, NULL) < 0.5);
    test_assert(strtod(stop_offset, NULL) > 0.5);
    showcase_click(world, s, TEXTURE_STOP);
    texture_expect_reactor(world, s, false);
    showcase_expect(world, s, "get showcase Showcase.mut.last", "\"reactor_stop\"");

    ecs_os_free(start);
    ecs_os_free(stop);
    ecs_os_free(start_offset);
    ecs_os_free(stop_offset);
    ecs_fini(world);
}

void Texture_screen_frame_does_nothing(void) {
    // uishowcase/texture
    ecs_entity_t s = 0;
    ecs_world_t *world = showcase_load(&s);
    const char *state = "{clicks: 0, shadows: true, sun: 2, selected: false, last: \"none\", "
        "lamp: false, paint: 0, reactor: false}";

    showcase_hover(world, s, TEXTURE_FRAME, "#0");
    ecs_os_free(texture_hit_slice(world, s));
    showcase_click(world, s, TEXTURE_FRAME);
    showcase_expect(world, s, "get showcase Showcase.mut", state);
    texture_expect_reactor(world, s, false);

    showcase_key(world, s, "R");
    showcase_hover(world, s, TEXTURE_FRAME, "#0");
    showcase_click(world, s, TEXTURE_FRAME);
    texture_expect_reactor(world, s, true);
    showcase_expect(world, s, "get showcase Showcase.mut.last", "\"reactor\"");

    ecs_fini(world);
}

void Texture_reactor_key_toggles(void) {
    // uishowcase/texture
    ecs_entity_t s = 0;
    ecs_world_t *world = showcase_load(&s);

    showcase_key(world, s, "R");
    texture_expect_reactor(world, s, true);
    showcase_expect(world, s, "get showcase Showcase.mut.last", "\"reactor\"");

    showcase_key(world, s, "R");
    texture_expect_reactor(world, s, false);

    showcase_hover(world, s, TEXTURE_START, TEXTURE_START_PATH);
    showcase_click(world, s, TEXTURE_START);
    showcase_key(world, s, "R");
    texture_expect_reactor(world, s, false);

    ecs_fini(world);
}
