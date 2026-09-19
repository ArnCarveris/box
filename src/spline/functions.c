/**
 * @file spline/functions.c
 * @brief Script functions that sample a curve entity.
 *
 * The functions live under the module, so a script that uses box calls them as
 * spline.position(curve, t). The by parameter functions take a value from 0 to
 * 1, the at functions a distance in world units along the curve.
 */

#include "spline_internal.h"

/** Sample the curve an argument list names.
 *
 * @param ctx The call context
 * @param argv The arguments: the curve entity and a parameter or a distance
 * @param by_distance true to read the second argument as a distance
 * @param out Receives the sample, the curve start when the entity holds none
 */
static void box_spline_fn_sample(
    const ecs_function_ctx_t *ctx,
    const ecs_value_t *argv,
    bool by_distance,
    box_spline_sample_t *out)
{
    ecs_entity_t curve = *(ecs_entity_t*)argv[0].ptr;
    float value = *(ecs_f32_t*)argv[1].ptr;

    out->position = vec3_new(0, 0, 0);
    out->tangent = vec3_new(0, 0, 1);
    out->normal = vec3_new(0, 1, 0);
    if (by_distance) {
        box_spline_sample_at(ctx->world, curve, value, out);
    } else {
        box_spline_sample(ctx->world, curve, value, out);
    }
}

/** spline.position: the point of a curve at a parameter.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the point
 */
static void box_spline_fn_position(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    box_spline_sample_t sample;
    (void)argc;

    box_spline_fn_sample(ctx, argv, false, &sample);
    *(vec3*)result->ptr = sample.position;
}

/** spline.tangent: the direction of travel of a curve at a parameter.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the unit tangent
 */
static void box_spline_fn_tangent(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    box_spline_sample_t sample;
    (void)argc;

    box_spline_fn_sample(ctx, argv, false, &sample);
    *(vec3*)result->ptr = sample.tangent;
}

/** spline.normal: the up of a curve at a parameter.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the unit normal
 */
static void box_spline_fn_normal(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    box_spline_sample_t sample;
    (void)argc;

    box_spline_fn_sample(ctx, argv, false, &sample);
    *(vec3*)result->ptr = sample.normal;
}

/** spline.point_at: the point of a curve at a distance along it.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the point
 */
static void box_spline_fn_point_at(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    box_spline_sample_t sample;
    (void)argc;

    box_spline_fn_sample(ctx, argv, true, &sample);
    *(vec3*)result->ptr = sample.position;
}

/** spline.tangent_at: the direction of travel at a distance along a curve.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the unit tangent
 */
static void box_spline_fn_tangent_at(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    box_spline_sample_t sample;
    (void)argc;

    box_spline_fn_sample(ctx, argv, true, &sample);
    *(vec3*)result->ptr = sample.tangent;
}

/** spline.normal_at: the up of a curve at a distance along it.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the unit normal
 */
static void box_spline_fn_normal_at(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    box_spline_sample_t sample;
    (void)argc;

    box_spline_fn_sample(ctx, argv, true, &sample);
    *(vec3*)result->ptr = sample.normal;
}

/** spline.length: the arc length of a curve.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the length in world units
 */
static void box_spline_fn_length(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_entity_t curve = *(ecs_entity_t*)argv[0].ptr;
    (void)argc;

    *(ecs_f32_t*)result->ptr = box_spline_length(ctx->world, curve);
}

/** Register one function that samples a curve and returns a vector.
 *
 * @param world The world
 * @param module The module the function is registered under
 * @param name Name of the function
 * @param second Name of the second parameter
 * @param callback The implementation
 */
static void box_spline_function(
    ecs_world_t *world,
    ecs_entity_t module,
    const char *name,
    const char *second,
    ecs_function_callback_t callback)
{
    ecs_function(world, {
        .name = name,
        .parent = module,
        .return_type = ecs_id(vec3),
        .params = {
            { .name = "spline", .type = ecs_id(ecs_entity_t) },
            { .name = second, .type = ecs_id(ecs_f32_t) }
        },
        .callback = callback
    });
}

void box_spline_functions_init(
    ecs_world_t *world,
    ecs_entity_t module)
{
    box_spline_function(world, module, "position", "t", box_spline_fn_position);
    box_spline_function(world, module, "tangent", "t", box_spline_fn_tangent);
    box_spline_function(world, module, "normal", "t", box_spline_fn_normal);
    box_spline_function(world, module, "point_at", "distance", box_spline_fn_point_at);
    box_spline_function(world, module, "tangent_at", "distance", box_spline_fn_tangent_at);
    box_spline_function(world, module, "normal_at", "distance", box_spline_fn_normal_at);

    ecs_function(world, {
        .name = "length",
        .parent = module,
        .return_type = ecs_id(ecs_f32_t),
        .params = {
            { .name = "spline", .type = ecs_id(ecs_entity_t) }
        },
        .callback = box_spline_fn_length
    });
}
