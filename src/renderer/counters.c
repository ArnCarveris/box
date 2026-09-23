/**
 * @file renderer/counters.c
 * @brief Triangle math and the table lookups of the per frame counts.
 */

#include "counters.h"
#include <string.h>

uint64_t box_count_triangles(
    box_count_topology_t topology,
    uint32_t vertices,
    uint32_t instances)
{
    uint64_t per_instance = 0;
    if (topology == BOX_COUNT_TRIANGLES) {
        per_instance = vertices / 3;
    } else if (topology == BOX_COUNT_STRIP && vertices >= 3) {
        per_instance = vertices - 2;
    }

    return per_instance * instances;
}

void box_count_name(
    char *dst,
    const char *name,
    uint64_t length)
{
    uint64_t i = 0;
    while (name && i < length && i + 1 < BOX_COUNT_NAME_MAX && name[i]) {
        dst[i] = name[i];
        i ++;
    }

    dst[i] = '\0';
}

int32_t box_count_pass(
    box_counters_t *c,
    const char *name,
    bool compute)
{
    char key[BOX_COUNT_NAME_MAX];
    box_count_name(key, name, UINT64_MAX);
    int32_t result = -1;
    uint32_t i;
    for (i = 0; i < c->pass_count; i ++) {
        box_count_pass_t *p = &c->passes[i];
        if (p->compute == compute && !strcmp(p->name, key)) {
            result = (int32_t)i;
            goto done;
        }
    }

    if (c->pass_count < BOX_COUNT_PASS_MAX) {
        box_count_pass_t *p = &c->passes[c->pass_count];
        memset(p, 0, sizeof(*p));
        memcpy(p->name, key, sizeof(key));
        p->compute = compute;
        result = (int32_t)c->pass_count;
        c->pass_count ++;
    }

done:
    return result;
}

int32_t box_count_pipeline(
    box_counters_t *c,
    const void *handle,
    const char *name,
    bool compute)
{
    int32_t result = -1;
    uint32_t i;
    for (i = 0; i < c->pipeline_count; i ++) {
        if (c->pipelines[i].handle == handle) {
            result = (int32_t)i;
            goto done;
        }
    }

    if (c->pipeline_count < BOX_COUNT_PIPELINE_MAX) {
        box_count_pipeline_t *p = &c->pipelines[c->pipeline_count];
        memset(p, 0, sizeof(*p));
        p->handle = handle;
        box_count_name(p->name, name, UINT64_MAX);
        p->compute = compute;
        result = (int32_t)c->pipeline_count;
        c->pipeline_count ++;
    }

done:
    return result;
}

int32_t box_count_bind(
    box_counters_t *c,
    uint32_t pass,
    uint32_t slot)
{
    int32_t result = -1;
    uint32_t i;
    for (i = 0; i < c->bind_count; i ++) {
        if (c->binds[i].pass == pass && c->binds[i].slot == slot) {
            result = (int32_t)i;
            goto done;
        }
    }

    if (c->bind_count < BOX_COUNT_BIND_MAX) {
        box_count_bind_t *b = &c->binds[c->bind_count];
        memset(b, 0, sizeof(*b));
        b->pass = pass;
        b->slot = slot;
        result = (int32_t)c->bind_count;
        c->bind_count ++;
    }

done:
    return result;
}

const char* box_count_usage_name(
    uint32_t usage)
{
    static const char *names[BOX_COUNT_USAGE_COUNT] = {
        "indirect", "vertex", "index", "uniform", "storage", "readback", "query", "other"
    };
    const char *result = names[BOX_COUNT_USAGE_OTHER];
    if (usage < BOX_COUNT_USAGE_COUNT) {
        result = names[usage];
    }

    return result;
}
