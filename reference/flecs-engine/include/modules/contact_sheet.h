#ifndef FLECS_ENGINE_CONTACT_SHEET_H
#define FLECS_ENGINE_CONTACT_SHEET_H

#define FLECS_SHEET_GLYPH_W (5)
#define FLECS_SHEET_GLYPH_H (7)
#define FLECS_SHEET_MAX_CELLS (12)
#define FLECS_SHEET_MAX_RECTS (2 * FLECS_SHEET_MAX_CELLS + 1)
#define FLECS_SHEET_LABEL_MAX (32)

int32_t flecsEngine_sheetTextWidth(
    const char *text,
    int32_t scale);

int32_t flecsEngine_sheetTextHeight(
    int32_t scale);

void flecsEngine_sheetText(
    uint8_t *rgb,
    int32_t width,
    int32_t height,
    int32_t x,
    int32_t y,
    int32_t scale,
    const uint8_t *color,
    const char *text);

typedef struct {
    const uint8_t *rgb;
    int32_t width;
    int32_t height;
    const char *label;
} FlecsSheetCell;

typedef struct {
    FlecsSheetCell hero;
    FlecsSheetCell cells[FLECS_SHEET_MAX_CELLS];
    int32_t columns;
    int32_t rows;
    int32_t cell_width;
    int32_t cell_height;
    FlecsSheetCell row2[FLECS_SHEET_MAX_CELLS];
    int32_t row2_columns;
    int32_t row2_rows;
    const char *caption;
} FlecsSheetDesc;

typedef struct {
    char label[FLECS_SHEET_LABEL_MAX];
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} FlecsSheetRect;

typedef struct {
    uint8_t *rgb;
    int32_t width;
    int32_t height;
    FlecsSheetRect rects[FLECS_SHEET_MAX_RECTS];
    int32_t rect_count;
    int32_t caption_y;
    int32_t caption_height;
} FlecsSheetImage;

void flecsEngine_sheetRow2Cell(
    const FlecsSheetDesc *desc,
    int32_t *width_out,
    int32_t *height_out);

bool flecsEngine_sheetBuild(
    const FlecsSheetDesc *desc,
    FlecsSheetImage *out);

void flecsEngine_sheetFini(
    FlecsSheetImage *image);

#endif
