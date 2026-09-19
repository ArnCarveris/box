/**
 * @file ui/font.c
 * @brief Parses font files and rasterizes their glyphs into an atlas that grows.
 *
 * One atlas holds one font file at one em size and is shared by every element
 * that asks for that pair. A glyph is rasterized the first time a string needs
 * it and packed on a shelf; when the last shelf is full the atlas doubles,
 * which keeps every glyph where it is and changes every uv, so the epoch of the
 * atlas is raised and the elements that drew with it build their quads again.
 * The pixels go to the renderer through the box.texture module, so an atlas is
 * an ordinary texture that a glyph quad binds in its albedo slot.
 */

#include "ui_internal.h"
#include "../shader/shader_internal.h"
#include "../texture/texture_internal.h"
#include <stdio.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>

/** Texels kept between two glyphs so a mip of the atlas cannot bleed one into another */
#define BOX_UI_GLYPH_PAD (1)

/** Read a font file into a newly allocated buffer.
 *
 * @param path The file
 * @param size Receives the byte count
 * @return The bytes, owned by the caller, or NULL when the file cannot be read
 */
static uint8_t* box_ui_font_read(
    const char *path,
    long *size)
{
    uint8_t *result = NULL;
    *size = 0;
    FILE *f = fopen(path, "rb");
    if (!f) {
        goto done;
    }

    fseek(f, 0, SEEK_END);
    long bytes = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (bytes <= 0) {
        fclose(f);
        goto done;
    }

    result = ecs_os_malloc((ecs_size_t)bytes);
    *size = (long)fread(result, 1, (size_t)bytes, f);
    fclose(f);
done:
    return result;
}

/** Parse the file of an atlas and read the vertical metrics of its font.
 *
 * @param atlas The atlas, its path and size filled in
 */
static void box_ui_font_open(
    box_ui_atlas_t *atlas)
{
    char resolved[BOX_SHADER_PATH_MAX];
    const char *path = atlas->path;
    if (!box_shader_resolve(path, resolved, sizeof(resolved))) {
        ecs_err("box.ui: no font at '%s'", path);
        atlas->failed = true;
        goto done;
    }

    long bytes = 0;
    atlas->data = box_ui_font_read(resolved, &bytes);
    if (!atlas->data) {
        ecs_err("box.ui: cannot read font '%s'", resolved);
        atlas->failed = true;
        goto done;
    }

    stbtt_fontinfo *info = ecs_os_calloc_t(stbtt_fontinfo);
    atlas->font = info;
    if (!stbtt_InitFont(info, atlas->data, stbtt_GetFontOffsetForIndex(atlas->data, 0))) {
        ecs_err("box.ui: cannot parse font '%s'", resolved);
        atlas->failed = true;
        goto done;
    }

    int ascent, descent, gap;
    atlas->scale = stbtt_ScaleForPixelHeight(info, atlas->size);
    stbtt_GetFontVMetrics(info, &ascent, &descent, &gap);
    atlas->ascent = (float)ascent * atlas->scale;
    atlas->descent = -(float)descent * atlas->scale;
    atlas->line_gap = (float)gap * atlas->scale;
    atlas->edge = BOX_UI_ATLAS_SIZE;
    atlas->coverage = ecs_os_calloc((ecs_size_t)(atlas->edge * atlas->edge));
done:
    return;
}

/** Double the atlas, keeping every glyph on the row and column it sits on.
 *
 * @param atlas The atlas
 * @return true when the atlas grew, false when it is already the largest one
 */
static bool box_ui_font_grow(
    box_ui_atlas_t *atlas)
{
    bool result = false;
    int32_t edge = atlas->edge * 2;
    if (edge > BOX_TEXTURE_MAX_SIZE) {
        goto done;
    }

    uint8_t *pixels = ecs_os_calloc((ecs_size_t)(edge * edge));
    int32_t y;
    for (y = 0; y < atlas->edge; y ++) {
        ecs_os_memcpy(pixels + (ecs_size_t)y * edge,
            atlas->coverage + (ecs_size_t)y * atlas->edge, atlas->edge);
    }

    ecs_os_free(atlas->coverage);
    atlas->coverage = pixels;
    atlas->edge = edge;
    atlas->epoch ++;
    atlas->dirty = true;
    result = true;
done:
    return result;
}

/** Find room for a bitmap on a shelf of the atlas, growing it when there is none.
 *
 * @param world The world
 * @param atlas The atlas
 * @param width Width of the bitmap in texels
 * @param height Height of the bitmap in texels
 * @param out Receives the column and the row the bitmap goes at
 * @return true when the bitmap fits
 */
static bool box_ui_font_pack(
    ecs_world_t *world,
    box_ui_atlas_t *atlas,
    int32_t width,
    int32_t height,
    int32_t *out)
{
    bool result = false;
    int32_t step = BOX_UI_GLYPH_PAD;
    while (!result) {
        if (atlas->pen_x + width + step <= atlas->edge &&
            atlas->pen_y + height + step <= atlas->edge)
        {
            out[0] = atlas->pen_x;
            out[1] = atlas->pen_y;
            atlas->pen_x += width + step;
            if (height > atlas->shelf) {
                atlas->shelf = height;
            }

            result = true;
            continue;
        }

        if (atlas->pen_x > 0 && atlas->pen_y + atlas->shelf + step + height + step <=
            atlas->edge)
        {
            atlas->pen_y += atlas->shelf + step;
            atlas->pen_x = 0;
            atlas->shelf = 0;
            continue;
        }

        if (!box_ui_font_grow(atlas)) {
            ecs_err("box.ui: the atlas of '%s' at %.0f px is full",
                atlas->path, (double)atlas->size);
            goto done;
        }

        box_ui_fonts(world)->grown = true;
    }

done:
    return result;
}

const box_ui_glyph_t* box_ui_atlas_glyph(
    ecs_world_t *world,
    box_ui_atlas_t *atlas,
    int32_t codepoint)
{
    box_ui_glyph_t *result = ecs_map_get_deref(&atlas->glyphs,
        box_ui_glyph_t, (ecs_map_key_t)codepoint);
    if (result || atlas->failed) {
        goto done;
    }

    stbtt_fontinfo *info = atlas->font;
    int advance, lsb, x0, y0, x1, y1;
    stbtt_GetCodepointHMetrics(info, codepoint, &advance, &lsb);
    stbtt_GetCodepointBitmapBox(info, codepoint, atlas->scale, atlas->scale,
        &x0, &y0, &x1, &y1);

    box_ui_glyph_t glyph = {
        .advance = (float)advance * atlas->scale,
        .bearing_x = (float)x0,
        .top = (float)y0,
        .width = x1 - x0,
        .height = y1 - y0
    };

    if (glyph.width > 0 && glyph.height > 0) {
        int32_t at[2];
        if (!box_ui_font_pack(world, atlas, glyph.width, glyph.height, at)) {
            goto done;
        }

        glyph.x = at[0];
        glyph.y = at[1];
        stbtt_MakeCodepointBitmap(info,
            atlas->coverage + (ecs_size_t)glyph.y * atlas->edge + glyph.x,
            glyph.width, glyph.height, atlas->edge, atlas->scale, atlas->scale,
            codepoint);
        atlas->dirty = true;
    }

    result = ecs_map_insert_alloc_t(&atlas->glyphs, box_ui_glyph_t,
        (ecs_map_key_t)codepoint);
    *result = glyph;
done:
    return result;
}

box_ui_atlas_t* box_ui_atlas(
    ecs_world_t *world,
    const char *path,
    float size)
{
    UiFonts *fonts = box_ui_fonts(world);
    const char *file = path && path[0] ? path : BOX_UI_FONT;
    float em = size > 0 ? size : BOX_UI_FONT_SIZE;
    box_ui_atlas_t *result = NULL;
    int32_t i, count = ecs_vec_count(&fonts->atlases);
    for (i = 0; i < count && !result; i ++) {
        box_ui_atlas_t *atlas = ecs_vec_get_t(&fonts->atlases,
            box_ui_atlas_t*, i)[0];
        if (atlas->size == em && !ecs_os_strcmp(atlas->path, file)) {
            result = atlas;
        }
    }

    if (result) {
        goto done;
    }

    result = ecs_os_calloc_t(box_ui_atlas_t);
    result->path = ecs_os_strdup(file);
    result->size = em;
    ecs_map_init(&result->glyphs, NULL);
    box_ui_font_open(result);
    ecs_vec_append_t(NULL, &fonts->atlases, box_ui_atlas_t*)[0] = result;
done:
    return result;
}

box_ui_atlas_t* box_ui_atlas_of(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const Font *font = ecs_get(world, e, Font);
    ecs_entity_t cur = e;
    while (!font && cur) {
        cur = box_hierarchy_parent(world, cur);
        if (cur) {
            font = ecs_get(world, cur, Font);
        }
    }

    return box_ui_atlas(world, font ? font->path : NULL, font ? font->size : 0);
}

/** Give an atlas the prefab its glyph quads take their material from.
 *
 * The material is what makes a glyph composite cleanly over what is behind it.
 * An alpha blended surface is a glass surface in this engine: the fragment
 * stage adds the environment specular of the surface where the fragment is
 * transparent, which would leave a lit box around every glyph, and it lets the
 * scene behind through the tint of the albedo. A nearly black metal has no
 * specular of its own and a tint of one, so the only thing the quad adds is the
 * emissive color, which the build takes from the Rgb of the element. The values
 * live on one prefab per atlas, so a glyph carries an IsA pair instead of three
 * components of its own.
 *
 * @param world The world
 * @param atlas The atlas
 */
static void box_ui_atlas_prefab(
    ecs_world_t *world,
    box_ui_atlas_t *atlas)
{
    if (atlas->prefab) {
        goto done;
    }

    atlas->prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, atlas->prefab, PbrTextures, { .albedo = atlas->texture });
    ecs_set(world, atlas->prefab, BlendMode, { AlphaBlend });
    ecs_set(world, atlas->prefab, PbrMaterial, { .metallic = 1, .roughness = 0 });
    ecs_set(world, atlas->prefab, Rgb, { 1, 1, 1 });
done:
    return;
}

/** Hand the coverage of one atlas to the texture module as an RGBA8 image.
 *
 * The glyph carries its coverage in the alpha channel and leaves the color
 * white, so the albedo of a quad is the Rgb of its element and the blend of the
 * quad weighs it by the coverage of the glyph.
 *
 * @param world The world
 * @param atlas The atlas
 */
static void box_ui_atlas_publish(
    ecs_world_t *world,
    box_ui_atlas_t *atlas)
{
    int32_t texels = atlas->edge * atlas->edge;
    uint8_t *pixels = ecs_os_malloc((ecs_size_t)texels * 4);
    int32_t i;
    for (i = 0; i < texels; i ++) {
        pixels[i * 4] = 255;
        pixels[i * 4 + 1] = 255;
        pixels[i * 4 + 2] = 255;
        pixels[i * 4 + 3] = atlas->coverage[i];
    }

    if (!atlas->texture) {
        atlas->texture = ecs_new(world);
    }

    box_texture_set_image(world, atlas->texture, pixels, atlas->edge, atlas->edge);
    ecs_os_free(pixels);
    box_ui_atlas_prefab(world, atlas);
    atlas->dirty = false;
}

void box_ui_atlas_upload(
    ecs_world_t *world)
{
    UiFonts *fonts = box_ui_fonts(world);
    int32_t i, count = ecs_vec_count(&fonts->atlases);
    for (i = 0; i < count; i ++) {
        box_ui_atlas_t *atlas = ecs_vec_get_t(&fonts->atlases,
            box_ui_atlas_t*, i)[0];
        if (atlas->dirty) {
            box_ui_atlas_publish(world, atlas);
        }
    }
}

void box_ui_atlas_fini(
    UiFonts *fonts)
{
    int32_t i, count = ecs_vec_count(&fonts->atlases);
    for (i = 0; i < count; i ++) {
        box_ui_atlas_t *atlas = ecs_vec_get_t(&fonts->atlases,
            box_ui_atlas_t*, i)[0];
        ecs_map_iter_t mit = ecs_map_iter(&atlas->glyphs);
        while (ecs_map_next(&mit)) {
            ecs_os_free(ecs_map_ptr(&mit));
        }

        ecs_map_fini(&atlas->glyphs);
        ecs_os_free(atlas->coverage);
        ecs_os_free(atlas->font);
        ecs_os_free(atlas->data);
        ecs_os_free(atlas->path);
        ecs_os_free(atlas);
    }
}
