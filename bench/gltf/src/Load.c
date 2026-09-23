/**
 * @file Load.c
 * @brief Benchmarks for loading a glTF file into entities, meshes and materials.
 *
 * Each case writes the model it loads as a binary glTF in /tmp first, so the
 * benchmark carries no data files and can size the model it measures.
 */

#include <box.h>
#include <box/gltf.h>
#include <bake_bench.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Chunk type of the json chunk of a glb file */
#define BENCH_GLB_JSON (0x4E4F534Au)

/** Chunk type of the binary chunk of a glb file */
#define BENCH_GLB_BIN (0x004E4942u)

/** Append a little endian 32 bit word to a file.
 *
 * @param f The file
 * @param value The word
 */
static void bench_word(
    FILE *f,
    uint32_t value)
{
    uint8_t bytes[4] = {
        (uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16), (uint8_t)(value >> 24)
    };

    fwrite(bytes, 1, 4, f);
}

/** Build the vertex and index streams of a grid mesh.
 *
 * @param side Vertices per side of the grid
 * @param bin Receives the buffer, owned by the caller
 * @param indices Receives the number of indices
 * @return Number of bytes in the buffer
 */
static uint32_t bench_grid(
    int32_t side,
    uint8_t **bin,
    uint32_t *indices)
{
    int32_t vertices = side * side;
    int32_t quads = (side - 1) * (side - 1);
    uint32_t floats = (uint32_t)vertices * 8;
    uint32_t bytes = floats * 4 + (uint32_t)quads * 6 * 4;
    float *pos = malloc(bytes);
    float *nrm = pos + vertices * 3;
    float *uv = nrm + vertices * 3;
    uint32_t *idx = (uint32_t*)(uv + vertices * 2);
    int32_t x, y, n = 0;

    for (y = 0; y < side; y ++) {
        for (x = 0; x < side; x ++) {
            int32_t v = y * side + x;

            pos[v * 3] = (float)x;
            pos[v * 3 + 1] = (float)((x * 7 + y * 13) % 11) * 0.1f;
            pos[v * 3 + 2] = (float)y;
            nrm[v * 3] = 0;
            nrm[v * 3 + 1] = 1;
            nrm[v * 3 + 2] = 0;
            uv[v * 2] = (float)x / (float)side;
            uv[v * 2 + 1] = (float)y / (float)side;
        }
    }

    for (y = 0; y < side - 1; y ++) {
        for (x = 0; x < side - 1; x ++) {
            uint32_t a = (uint32_t)(y * side + x);
            uint32_t c = a + (uint32_t)side;

            idx[n ++] = a;
            idx[n ++] = a + 1;
            idx[n ++] = c;
            idx[n ++] = a + 1;
            idx[n ++] = c + 1;
            idx[n ++] = c;
        }
    }

    *bin = (uint8_t*)pos;
    *indices = (uint32_t)n;

    return bytes;
}

/** Write a glb file holding one grid mesh that a number of nodes share.
 *
 * @param path The file to write
 * @param nodes Number of nodes in the scene
 * @param side Vertices per side of the grid mesh
 */
static void bench_glb(
    const char *path,
    int32_t nodes,
    int32_t side)
{
    uint8_t *bin = NULL;
    uint32_t indices = 0;
    uint32_t bin_size = bench_grid(side, &bin, &indices);
    uint32_t vertices = (uint32_t)(side * side);
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    int32_t i;

    ecs_strbuf_append(&buf, "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,"
        "\"scenes\":[{\"nodes\":[");
    for (i = 0; i < nodes; i ++) {
        ecs_strbuf_append(&buf, "%s%d", i ? "," : "", i);
    }

    ecs_strbuf_append(&buf, "]}],\"nodes\":[");
    for (i = 0; i < nodes; i ++) {
        ecs_strbuf_append(&buf, "%s{\"mesh\":0,\"translation\":[%d,0,0]}", i ? "," : "", i);
    }

    ecs_strbuf_append(&buf,
        "],\"meshes\":[{\"primitives\":[{\"attributes\":"
        "{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2},\"indices\":3,\"material\":0}]}],"
        "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.7,0.7,0.7,1]}}],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\","
        "\"min\":[0,0,0],\"max\":[%d,1,%d]},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\"},"
        "{\"bufferView\":2,\"componentType\":5126,\"count\":%u,\"type\":\"VEC2\"},"
        "{\"bufferView\":3,\"componentType\":5125,\"count\":%u,\"type\":\"SCALAR\"}],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":%u},"
        "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u},"
        "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u},"
        "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}],"
        "\"buffers\":[{\"byteLength\":%u}]}",
        vertices, side - 1, side - 1, vertices, vertices, indices,
        vertices * 12, vertices * 12, vertices * 12, vertices * 24, vertices * 8,
        vertices * 32, indices * 4, bin_size);

    char *json = ecs_strbuf_get(&buf);
    uint32_t json_size = (uint32_t)strlen(json);
    uint32_t json_pad = (4 - (json_size & 3)) & 3;
    FILE *f = fopen(path, "wb");

    bench_word(f, 0x46546C67u);
    bench_word(f, 2);
    bench_word(f, 12 + 8 + json_size + json_pad + 8 + bin_size);
    bench_word(f, json_size + json_pad);
    bench_word(f, BENCH_GLB_JSON);
    fwrite(json, 1, json_size, f);
    fwrite("   ", 1, json_pad, f);
    bench_word(f, bin_size);
    bench_word(f, BENCH_GLB_BIN);
    fwrite(bin, 1, bin_size, f);
    fclose(f);

    ecs_os_free(json);
    free(bin);
}

void Load_mesh_100k(
    bench_t *b)
{
    // meshes/gltf
    const char *path = "/tmp/box_bench_gltf_mesh.glb";
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    bench_glb(path, 1, 225);
    bench_set_items(b, 224 * 224 * 2);

    ecs_entity_t e = ecs_new(world);
    while (bench_iter(b)) {
        bench_pause(b);
        ecs_delete(world, e);
        e = ecs_new(world);
        bench_resume(b);

        ecs_set(world, e, Gltf, { .path = ecs_os_strdup(path) });
        bench_keep((int)ecs_lookup_child(world, e, "node0"));
    }

    ecs_fini(world);
}

void Load_nodes_500(
    bench_t *b)
{
    // meshes/gltf
    const char *path = "/tmp/box_bench_gltf_nodes.glb";
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    bench_glb(path, 500, 4);
    bench_set_items(b, 500);

    ecs_entity_t e = ecs_new(world);
    while (bench_iter(b)) {
        bench_pause(b);
        ecs_delete(world, e);
        e = ecs_new(world);
        bench_resume(b);

        ecs_set(world, e, Gltf, { .path = ecs_os_strdup(path) });
        bench_keep((int)ecs_lookup_child(world, e, "node499"));
    }

    ecs_fini(world);
}
