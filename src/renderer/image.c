/**
 * @file renderer/image.c
 * @brief Image file writing through stb_image_write.
 */

#include "renderer_internal.h"
#include <stdio.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>

/** stb write callback that appends to a FILE.
 *
 * @param context The FILE
 * @param data Bytes to write
 * @param size Byte count
 */
static void box_png_write(
    void *context,
    void *data,
    int size)
{
    fwrite(data, 1, (size_t)size, context);
}

int box_image_write_png(
    const char *path,
    uint32_t width,
    uint32_t height,
    uint32_t stride,
    const void *pixels)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "box: cannot write '%s'\n", path);
        return -1;
    }
    int ok = stbi_write_png_to_func(box_png_write, f, (int)width, (int)height, 4, pixels, (int)stride);
    fclose(f);
    return ok ? 0 : -1;
}
