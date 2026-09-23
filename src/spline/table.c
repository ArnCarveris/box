/**
 * @file spline/table.c
 * @brief The cached arc length table of a curve and the sampling of an entity.
 *
 * The table holds the distance at evenly spaced parameters, so a distance maps
 * to a parameter with a binary search and one interpolation. Sampling an entity
 * reads the table cached on it.
 */

#include "spline_internal.h"
#include <math.h>

void box_spline_table_fini(
    SplineTable *table)
{
    ecs_vec_fini_t(NULL, &table->distance, float);
    table->segments = 0;
    table->steps = 0;
    table->length = 0;
}

void box_spline_table_build(
    const Spline *spline,
    SplineTable *table)
{
    int32_t segments = box_spline_segments(spline);
    int32_t samples = segments * BOX_SPLINE_STEPS + 1;
    box_spline_sample_t sample;
    float distance = 0;
    int32_t i;

    box_spline_table_fini(table);
    table->segments = segments;
    table->steps = BOX_SPLINE_STEPS;
    ecs_vec_init_t(NULL, &table->distance, float, samples);
    ecs_vec_set_count_t(NULL, &table->distance, float, samples);

    float *values = ecs_vec_first_t(&table->distance, float);
    box_spline_eval(spline, 0, &sample);

    vec3 previous = sample.position;
    values[0] = 0;
    for (i = 1; i < samples; i ++) {
        box_spline_eval(spline, (float)i / (float)(samples - 1), &sample);
        distance += vec3_length(vec3_sub(sample.position, previous));
        previous = sample.position;
        values[i] = distance;
    }

    table->length = distance;
}

float box_spline_table_param(
    const SplineTable *table,
    float distance)
{
    int32_t count = table ? ecs_vec_count(&table->distance) : 0;
    float result = 0;
    if (count < 2 || table->length <= 0 || distance <= 0) {
        goto done;
    }

    if (distance >= table->length) {
        result = 1;
        goto done;
    }

    const float *values = ecs_vec_first_t(&table->distance, float);
    int32_t low = 0;
    int32_t high = count - 1;
    while (high - low > 1) {
        int32_t mid = (low + high) / 2;
        if (values[mid] <= distance) {
            low = mid;
        } else {
            high = mid;
        }
    }

    float span = values[high] - values[low];
    float fraction = span > 0 ? (distance - values[low]) / span : 0;
    result = ((float)low + fraction) / (float)(count - 1);
done:
    return result;
}

float box_spline_table_distance(
    const SplineTable *table,
    float t)
{
    int32_t count = table ? ecs_vec_count(&table->distance) : 0;
    float result = 0;
    if (count < 2) {
        goto done;
    }

    const float *values = ecs_vec_first_t(&table->distance, float);
    float scaled = fminf(fmaxf(t, 0.0f), 1.0f) * (float)(count - 1);
    int32_t index = (int32_t)floorf(scaled);
    if (index >= count - 1) {
        result = values[count - 1];
        goto done;
    }

    result = values[index] + (values[index + 1] - values[index]) * (scaled - (float)index);
done:
    return result;
}

const Spline* box_spline_get(
    const ecs_world_t *world,
    ecs_entity_t spline,
    const SplineTable **table_out)
{
    const Spline *result = NULL;
    if (table_out) {
        *table_out = NULL;
    }

    if (!spline || !ecs_is_alive(world, spline)) {
        goto done;
    }

    result = ecs_get(world, spline, Spline);
    if (result && table_out) {
        *table_out = ecs_get(world, spline, SplineTable);
    }

done:
    return result;
}

bool box_spline_sample(
    const ecs_world_t *world,
    ecs_entity_t spline,
    float t,
    box_spline_sample_t *out)
{
    const Spline *curve = box_spline_get(world, spline, NULL);
    bool result = false;
    if (!curve) {
        goto done;
    }

    box_spline_eval(curve, t, out);
    result = true;
done:
    return result;
}

bool box_spline_sample_at(
    const ecs_world_t *world,
    ecs_entity_t spline,
    float distance,
    box_spline_sample_t *out)
{
    const SplineTable *table = NULL;
    const Spline *curve = box_spline_get(world, spline, &table);
    bool result = false;
    if (!curve) {
        goto done;
    }

    float length = table ? table->length : 0;
    if (curve->loop && length > 0) {
        distance -= floorf(distance / length) * length;
    }

    box_spline_eval(curve, box_spline_table_param(table, distance), out);
    result = true;
done:
    return result;
}

float box_spline_length(
    const ecs_world_t *world,
    ecs_entity_t spline)
{
    const SplineTable *table = NULL;
    float result = 0;
    if (box_spline_get(world, spline, &table) && table) {
        result = table->length;
    }

    return result;
}
