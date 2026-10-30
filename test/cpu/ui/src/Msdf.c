/**
 * @file Msdf.c
 * @brief box_msdf_generate on the CPU: far texels saturate to their side, the median of near
 * texels and the true distance follow the nonzero fill, and the field matches a reference.
 *
 * The reference fields are the raw RGBA8 texels in test/assets/msdf/<case>.rgba. Run the suite
 * with BOX_MSDF_WRITE_REFERENCE set to write them anew from the current generator.
 */

#include <box.h>
#include <bake_test.h>
#include <msdf.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/** Start a contour at x, y */
#define MSDF_MOVE (0)

/** A line to x, y */
#define MSDF_LINE (1)

/** A quadratic curve through cx, cy to x, y */
#define MSDF_QUAD (2)

/** Lines a quadratic curve is flattened into for the expected distance and fill */
#define MSDF_FLATTEN (64)

/** Largest number of flattened lines of a shape */
#define MSDF_SEGMENTS (512)

/** Difference of a channel from the reference that is allowed */
#define MSDF_TOLERANCE (2)

/** A command of an outline, in font units with y up */
typedef struct msdf_cmd_t {
    int32_t op; /**< MSDF_MOVE, MSDF_LINE or MSDF_QUAD */
    double cx;  /**< Control x of a curve */
    double cy;  /**< Control y of a curve */
    double x;   /**< x of the end */
    double y;   /**< y of the end */
} msdf_cmd_t;

/* The cases call no flecs function that would set up the OS API they allocate with. */
void Msdf_setup(void) {
    ecs_os_set_api_defaults();
}

/** A square: the field of four lines and four right angle corners. */
void Msdf_square(void) {
    const char *name = "square";
    const msdf_cmd_t cmds[] = {
        { MSDF_MOVE, 0, 0, 8, 8 },
        { MSDF_LINE, 0, 0, 8, 32 },
        { MSDF_LINE, 0, 0, 32, 32 },
        { MSDF_LINE, 0, 0, 32, 8 },
    };

    int32_t i, s, x, y, c, count = (int32_t)(sizeof(cmds) / sizeof(cmds[0]));
    box_msdf_raster_t raster = {
        .scale = 1.0, .left = 0.0, .top = 40.0, .width = 40, .height = 40, .range = 4.0
    };
    int32_t bytes = raster.width * raster.height * 4;

    // Build the shape, and the outline flattened into lines for the expected distance and fill
    box_msdf_shape_t shape;
    box_msdf_shape_init(&shape);
    double seg[MSDF_SEGMENTS][4];
    int32_t segs = 0;
    double px = 0.0, py = 0.0, sx = 0.0, sy = 0.0;
    for (i = 0; i <= count; i ++) {
        if ((i == count || cmds[i].op == MSDF_MOVE) && i > 0 && (px != sx || py != sy)) {
            test_assert(segs < MSDF_SEGMENTS);
            seg[segs][0] = px; seg[segs][1] = py; seg[segs][2] = sx; seg[segs][3] = sy;
            segs ++;
        }

        if (i == count) {
            break;
        }

        const msdf_cmd_t *cmd = &cmds[i];
        if (cmd->op == MSDF_MOVE) {
            box_msdf_move_to(&shape, cmd->x, cmd->y);
            sx = cmd->x;
            sy = cmd->y;
        } else if (cmd->op == MSDF_LINE) {
            box_msdf_line_to(&shape, cmd->x, cmd->y);
            test_assert(segs < MSDF_SEGMENTS);
            seg[segs][0] = px; seg[segs][1] = py; seg[segs][2] = cmd->x; seg[segs][3] = cmd->y;
            segs ++;
        } else {
            box_msdf_quad_to(&shape, cmd->cx, cmd->cy, cmd->x, cmd->y);
            double ax = px, ay = py;
            for (s = 1; s <= MSDF_FLATTEN; s ++) {
                double t = (double)s / MSDF_FLATTEN, u = 1.0 - t;
                double bx = u * u * px + 2.0 * u * t * cmd->cx + t * t * cmd->x;
                double by = u * u * py + 2.0 * u * t * cmd->cy + t * t * cmd->y;
                test_assert(segs < MSDF_SEGMENTS);
                seg[segs][0] = ax; seg[segs][1] = ay; seg[segs][2] = bx; seg[segs][3] = by;
                segs ++;
                ax = bx;
                ay = by;
            }
        }

        px = cmd->x;
        py = cmd->y;
    }

    box_msdf_shape_finish(&shape);
    uint8_t *pixels = ecs_os_malloc_n(uint8_t, bytes);
    box_msdf_generate(&shape, &raster, pixels);
    box_msdf_shape_fini(&shape);

    // Far texels saturate to their side; near texels have the median and the true distance on
    // the side the nonzero fill gives, except within half a texel of the outline
    int32_t far = 0, near = 0, wrong_far = 0, wrong_median = 0, wrong_alpha = 0;
    for (y = 0; y < raster.height; y ++) {
        for (x = 0; x < raster.width; x ++) {
            double cx = raster.left + (x + 0.5) / raster.scale;
            double cy = raster.top - (y + 0.5) / raster.scale;
            double dist = 1e30;
            int32_t winding = 0;
            for (s = 0; s < segs; s ++) {
                double ax = seg[s][0], ay = seg[s][1], dx = seg[s][2] - ax, dy = seg[s][3] - ay;
                double len2 = dx * dx + dy * dy;
                double t = len2 > 0.0 ? ((cx - ax) * dx + (cy - ay) * dy) / len2 : 0.0;
                t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
                double ex = ax + t * dx - cx, ey = ay + t * dy - cy;
                dist = fmin(dist, sqrt(ex * ex + ey * ey));
                if ((ay <= cy) != (seg[s][3] <= cy)) {
                    double xc = ax + (cy - ay) / dy * dx;
                    if (xc > cx) {
                        winding += dy > 0.0 ? 1 : -1;
                    }
                }
            }

            bool inside = winding != 0;
            const uint8_t *t = &pixels[(y * raster.width + x) * 4];
            dist *= raster.scale;
            if (dist > raster.range) {
                far ++;
                for (c = 0; c < 4; c ++) {
                    wrong_far += t[c] != (inside ? 255 : 0);
                }

                continue;
            }

            if (dist < 0.5) {
                continue;
            }

            near ++;
            int32_t lo = t[0] < t[1] ? t[0] : t[1], hi = t[0] < t[1] ? t[1] : t[0];
            int32_t median = hi < t[2] ? hi : (lo > t[2] ? lo : t[2]);
            wrong_median += (median > 127) != inside;
            wrong_alpha += (t[3] > 127) != inside;
        }
    }

    test_assert(far > 0);
    test_assert(near > 0);
    test_int(wrong_far, 0);
    test_int(wrong_median, 0);
    test_int(wrong_alpha, 0);

    // The whole field matches the reference
    char path[4096];
    char *dir = box_app_locate("test/assets/msdf");
    snprintf(path, sizeof(path), "%s/%s.rgba", dir, name);
    ecs_os_free(dir);
    if (getenv("BOX_MSDF_WRITE_REFERENCE")) {
        FILE *w = fopen(path, "wb");
        test_assert(w != NULL);
        test_int((int32_t)fwrite(pixels, 1, (size_t)bytes, w), bytes);
        fclose(w);
    }

    FILE *f = fopen(path, "rb");
    test_assert(f != NULL);
    uint8_t *ref = ecs_os_malloc_n(uint8_t, bytes + 1);
    test_int((int32_t)fread(ref, 1, (size_t)bytes + 1, f), bytes);
    fclose(f);
    int32_t differ = 0;
    for (i = 0; i < bytes; i ++) {
        differ += abs((int32_t)pixels[i] - (int32_t)ref[i]) > MSDF_TOLERANCE;
    }

    test_int(differ, 0);
    ecs_os_free(ref);
    ecs_os_free(pixels);
}

/** A square with a square hole wound the other way: inside the hole is outside. */
void Msdf_square_hole(void) {
    const char *name = "square_hole";
    const msdf_cmd_t cmds[] = {
        { MSDF_MOVE, 0, 0, 6, 6 },
        { MSDF_LINE, 0, 0, 6, 34 },
        { MSDF_LINE, 0, 0, 34, 34 },
        { MSDF_LINE, 0, 0, 34, 6 },
        { MSDF_MOVE, 0, 0, 14, 14 },
        { MSDF_LINE, 0, 0, 26, 14 },
        { MSDF_LINE, 0, 0, 26, 26 },
        { MSDF_LINE, 0, 0, 14, 26 },
    };

    int32_t i, s, x, y, c, count = (int32_t)(sizeof(cmds) / sizeof(cmds[0]));
    box_msdf_raster_t raster = {
        .scale = 1.0, .left = 0.0, .top = 40.0, .width = 40, .height = 40, .range = 4.0
    };
    int32_t bytes = raster.width * raster.height * 4;

    // Build the shape, and the outline flattened into lines for the expected distance and fill
    box_msdf_shape_t shape;
    box_msdf_shape_init(&shape);
    double seg[MSDF_SEGMENTS][4];
    int32_t segs = 0;
    double px = 0.0, py = 0.0, sx = 0.0, sy = 0.0;
    for (i = 0; i <= count; i ++) {
        if ((i == count || cmds[i].op == MSDF_MOVE) && i > 0 && (px != sx || py != sy)) {
            test_assert(segs < MSDF_SEGMENTS);
            seg[segs][0] = px; seg[segs][1] = py; seg[segs][2] = sx; seg[segs][3] = sy;
            segs ++;
        }

        if (i == count) {
            break;
        }

        const msdf_cmd_t *cmd = &cmds[i];
        if (cmd->op == MSDF_MOVE) {
            box_msdf_move_to(&shape, cmd->x, cmd->y);
            sx = cmd->x;
            sy = cmd->y;
        } else if (cmd->op == MSDF_LINE) {
            box_msdf_line_to(&shape, cmd->x, cmd->y);
            test_assert(segs < MSDF_SEGMENTS);
            seg[segs][0] = px; seg[segs][1] = py; seg[segs][2] = cmd->x; seg[segs][3] = cmd->y;
            segs ++;
        } else {
            box_msdf_quad_to(&shape, cmd->cx, cmd->cy, cmd->x, cmd->y);
            double ax = px, ay = py;
            for (s = 1; s <= MSDF_FLATTEN; s ++) {
                double t = (double)s / MSDF_FLATTEN, u = 1.0 - t;
                double bx = u * u * px + 2.0 * u * t * cmd->cx + t * t * cmd->x;
                double by = u * u * py + 2.0 * u * t * cmd->cy + t * t * cmd->y;
                test_assert(segs < MSDF_SEGMENTS);
                seg[segs][0] = ax; seg[segs][1] = ay; seg[segs][2] = bx; seg[segs][3] = by;
                segs ++;
                ax = bx;
                ay = by;
            }
        }

        px = cmd->x;
        py = cmd->y;
    }

    box_msdf_shape_finish(&shape);
    uint8_t *pixels = ecs_os_malloc_n(uint8_t, bytes);
    box_msdf_generate(&shape, &raster, pixels);
    box_msdf_shape_fini(&shape);

    // Far texels saturate to their side; near texels have the median and the true distance on
    // the side the nonzero fill gives, except within half a texel of the outline
    int32_t far = 0, near = 0, wrong_far = 0, wrong_median = 0, wrong_alpha = 0;
    for (y = 0; y < raster.height; y ++) {
        for (x = 0; x < raster.width; x ++) {
            double cx = raster.left + (x + 0.5) / raster.scale;
            double cy = raster.top - (y + 0.5) / raster.scale;
            double dist = 1e30;
            int32_t winding = 0;
            for (s = 0; s < segs; s ++) {
                double ax = seg[s][0], ay = seg[s][1], dx = seg[s][2] - ax, dy = seg[s][3] - ay;
                double len2 = dx * dx + dy * dy;
                double t = len2 > 0.0 ? ((cx - ax) * dx + (cy - ay) * dy) / len2 : 0.0;
                t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
                double ex = ax + t * dx - cx, ey = ay + t * dy - cy;
                dist = fmin(dist, sqrt(ex * ex + ey * ey));
                if ((ay <= cy) != (seg[s][3] <= cy)) {
                    double xc = ax + (cy - ay) / dy * dx;
                    if (xc > cx) {
                        winding += dy > 0.0 ? 1 : -1;
                    }
                }
            }

            bool inside = winding != 0;
            const uint8_t *t = &pixels[(y * raster.width + x) * 4];
            dist *= raster.scale;
            if (dist > raster.range) {
                far ++;
                for (c = 0; c < 4; c ++) {
                    wrong_far += t[c] != (inside ? 255 : 0);
                }

                continue;
            }

            if (dist < 0.5) {
                continue;
            }

            near ++;
            int32_t lo = t[0] < t[1] ? t[0] : t[1], hi = t[0] < t[1] ? t[1] : t[0];
            int32_t median = hi < t[2] ? hi : (lo > t[2] ? lo : t[2]);
            wrong_median += (median > 127) != inside;
            wrong_alpha += (t[3] > 127) != inside;
        }
    }

    test_assert(far > 0);
    test_assert(near > 0);
    test_int(wrong_far, 0);
    test_int(wrong_median, 0);
    test_int(wrong_alpha, 0);

    // The whole field matches the reference
    char path[4096];
    char *dir = box_app_locate("test/assets/msdf");
    snprintf(path, sizeof(path), "%s/%s.rgba", dir, name);
    ecs_os_free(dir);
    if (getenv("BOX_MSDF_WRITE_REFERENCE")) {
        FILE *w = fopen(path, "wb");
        test_assert(w != NULL);
        test_int((int32_t)fwrite(pixels, 1, (size_t)bytes, w), bytes);
        fclose(w);
    }

    FILE *f = fopen(path, "rb");
    test_assert(f != NULL);
    uint8_t *ref = ecs_os_malloc_n(uint8_t, bytes + 1);
    test_int((int32_t)fread(ref, 1, (size_t)bytes + 1, f), bytes);
    fclose(f);
    int32_t differ = 0;
    for (i = 0; i < bytes; i ++) {
        differ += abs((int32_t)pixels[i] - (int32_t)ref[i]) > MSDF_TOLERANCE;
    }

    test_int(differ, 0);
    ecs_os_free(ref);
    ecs_os_free(pixels);
}

/** Two squares wound the same way that overlap: the union has no seam where the
 * edges of one contour lie inside the other. */
void Msdf_overlapping_contours(void) {
    const char *name = "overlapping_contours";
    const msdf_cmd_t cmds[] = {
        { MSDF_MOVE, 0, 0, 6, 6 },
        { MSDF_LINE, 0, 0, 6, 24 },
        { MSDF_LINE, 0, 0, 24, 24 },
        { MSDF_LINE, 0, 0, 24, 6 },
        { MSDF_MOVE, 0, 0, 16, 16 },
        { MSDF_LINE, 0, 0, 16, 34 },
        { MSDF_LINE, 0, 0, 34, 34 },
        { MSDF_LINE, 0, 0, 34, 16 },
    };

    int32_t i, s, x, y, c, count = (int32_t)(sizeof(cmds) / sizeof(cmds[0]));
    box_msdf_raster_t raster = {
        .scale = 1.0, .left = 0.0, .top = 40.0, .width = 40, .height = 40, .range = 4.0
    };
    int32_t bytes = raster.width * raster.height * 4;

    // Build the shape, and the outline flattened into lines for the expected distance and fill
    box_msdf_shape_t shape;
    box_msdf_shape_init(&shape);
    double seg[MSDF_SEGMENTS][4];
    int32_t segs = 0;
    double px = 0.0, py = 0.0, sx = 0.0, sy = 0.0;
    for (i = 0; i <= count; i ++) {
        if ((i == count || cmds[i].op == MSDF_MOVE) && i > 0 && (px != sx || py != sy)) {
            test_assert(segs < MSDF_SEGMENTS);
            seg[segs][0] = px; seg[segs][1] = py; seg[segs][2] = sx; seg[segs][3] = sy;
            segs ++;
        }

        if (i == count) {
            break;
        }

        const msdf_cmd_t *cmd = &cmds[i];
        if (cmd->op == MSDF_MOVE) {
            box_msdf_move_to(&shape, cmd->x, cmd->y);
            sx = cmd->x;
            sy = cmd->y;
        } else if (cmd->op == MSDF_LINE) {
            box_msdf_line_to(&shape, cmd->x, cmd->y);
            test_assert(segs < MSDF_SEGMENTS);
            seg[segs][0] = px; seg[segs][1] = py; seg[segs][2] = cmd->x; seg[segs][3] = cmd->y;
            segs ++;
        } else {
            box_msdf_quad_to(&shape, cmd->cx, cmd->cy, cmd->x, cmd->y);
            double ax = px, ay = py;
            for (s = 1; s <= MSDF_FLATTEN; s ++) {
                double t = (double)s / MSDF_FLATTEN, u = 1.0 - t;
                double bx = u * u * px + 2.0 * u * t * cmd->cx + t * t * cmd->x;
                double by = u * u * py + 2.0 * u * t * cmd->cy + t * t * cmd->y;
                test_assert(segs < MSDF_SEGMENTS);
                seg[segs][0] = ax; seg[segs][1] = ay; seg[segs][2] = bx; seg[segs][3] = by;
                segs ++;
                ax = bx;
                ay = by;
            }
        }

        px = cmd->x;
        py = cmd->y;
    }

    box_msdf_shape_finish(&shape);
    uint8_t *pixels = ecs_os_malloc_n(uint8_t, bytes);
    box_msdf_generate(&shape, &raster, pixels);
    box_msdf_shape_fini(&shape);

    // Far texels saturate to their side; near texels have the median and the true distance on
    // the side the nonzero fill gives, except within half a texel of the outline
    int32_t far = 0, near = 0, wrong_far = 0, wrong_median = 0, wrong_alpha = 0;
    for (y = 0; y < raster.height; y ++) {
        for (x = 0; x < raster.width; x ++) {
            double cx = raster.left + (x + 0.5) / raster.scale;
            double cy = raster.top - (y + 0.5) / raster.scale;
            double dist = 1e30;
            int32_t winding = 0;
            for (s = 0; s < segs; s ++) {
                double ax = seg[s][0], ay = seg[s][1], dx = seg[s][2] - ax, dy = seg[s][3] - ay;
                double len2 = dx * dx + dy * dy;
                double t = len2 > 0.0 ? ((cx - ax) * dx + (cy - ay) * dy) / len2 : 0.0;
                t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
                double ex = ax + t * dx - cx, ey = ay + t * dy - cy;
                dist = fmin(dist, sqrt(ex * ex + ey * ey));
                if ((ay <= cy) != (seg[s][3] <= cy)) {
                    double xc = ax + (cy - ay) / dy * dx;
                    if (xc > cx) {
                        winding += dy > 0.0 ? 1 : -1;
                    }
                }
            }

            bool inside = winding != 0;
            const uint8_t *t = &pixels[(y * raster.width + x) * 4];
            dist *= raster.scale;
            if (dist > raster.range) {
                far ++;
                for (c = 0; c < 4; c ++) {
                    wrong_far += t[c] != (inside ? 255 : 0);
                }

                continue;
            }

            if (dist < 0.5) {
                continue;
            }

            near ++;
            int32_t lo = t[0] < t[1] ? t[0] : t[1], hi = t[0] < t[1] ? t[1] : t[0];
            int32_t median = hi < t[2] ? hi : (lo > t[2] ? lo : t[2]);
            wrong_median += (median > 127) != inside;
            wrong_alpha += (t[3] > 127) != inside;
        }
    }

    test_assert(far > 0);
    test_assert(near > 0);
    test_int(wrong_far, 0);
    test_int(wrong_median, 0);
    test_int(wrong_alpha, 0);

    // The whole field matches the reference
    char path[4096];
    char *dir = box_app_locate("test/assets/msdf");
    snprintf(path, sizeof(path), "%s/%s.rgba", dir, name);
    ecs_os_free(dir);
    if (getenv("BOX_MSDF_WRITE_REFERENCE")) {
        FILE *w = fopen(path, "wb");
        test_assert(w != NULL);
        test_int((int32_t)fwrite(pixels, 1, (size_t)bytes, w), bytes);
        fclose(w);
    }

    FILE *f = fopen(path, "rb");
    test_assert(f != NULL);
    uint8_t *ref = ecs_os_malloc_n(uint8_t, bytes + 1);
    test_int((int32_t)fread(ref, 1, (size_t)bytes + 1, f), bytes);
    fclose(f);
    int32_t differ = 0;
    for (i = 0; i < bytes; i ++) {
        differ += abs((int32_t)pixels[i] - (int32_t)ref[i]) > MSDF_TOLERANCE;
    }

    test_int(differ, 0);
    ecs_os_free(ref);
    ecs_os_free(pixels);
}

/** A triangle with a very acute corner of about 15 degrees. */
void Msdf_spike(void) {
    const char *name = "spike";
    const msdf_cmd_t cmds[] = {
        { MSDF_MOVE, 0, 0, 6, 16 },
        { MSDF_LINE, 0, 0, 6, 24 },
        { MSDF_LINE, 0, 0, 36, 20 },
    };

    int32_t i, s, x, y, c, count = (int32_t)(sizeof(cmds) / sizeof(cmds[0]));
    box_msdf_raster_t raster = {
        .scale = 1.0, .left = 0.0, .top = 40.0, .width = 40, .height = 40, .range = 4.0
    };
    int32_t bytes = raster.width * raster.height * 4;

    // Build the shape, and the outline flattened into lines for the expected distance and fill
    box_msdf_shape_t shape;
    box_msdf_shape_init(&shape);
    double seg[MSDF_SEGMENTS][4];
    int32_t segs = 0;
    double px = 0.0, py = 0.0, sx = 0.0, sy = 0.0;
    for (i = 0; i <= count; i ++) {
        if ((i == count || cmds[i].op == MSDF_MOVE) && i > 0 && (px != sx || py != sy)) {
            test_assert(segs < MSDF_SEGMENTS);
            seg[segs][0] = px; seg[segs][1] = py; seg[segs][2] = sx; seg[segs][3] = sy;
            segs ++;
        }

        if (i == count) {
            break;
        }

        const msdf_cmd_t *cmd = &cmds[i];
        if (cmd->op == MSDF_MOVE) {
            box_msdf_move_to(&shape, cmd->x, cmd->y);
            sx = cmd->x;
            sy = cmd->y;
        } else if (cmd->op == MSDF_LINE) {
            box_msdf_line_to(&shape, cmd->x, cmd->y);
            test_assert(segs < MSDF_SEGMENTS);
            seg[segs][0] = px; seg[segs][1] = py; seg[segs][2] = cmd->x; seg[segs][3] = cmd->y;
            segs ++;
        } else {
            box_msdf_quad_to(&shape, cmd->cx, cmd->cy, cmd->x, cmd->y);
            double ax = px, ay = py;
            for (s = 1; s <= MSDF_FLATTEN; s ++) {
                double t = (double)s / MSDF_FLATTEN, u = 1.0 - t;
                double bx = u * u * px + 2.0 * u * t * cmd->cx + t * t * cmd->x;
                double by = u * u * py + 2.0 * u * t * cmd->cy + t * t * cmd->y;
                test_assert(segs < MSDF_SEGMENTS);
                seg[segs][0] = ax; seg[segs][1] = ay; seg[segs][2] = bx; seg[segs][3] = by;
                segs ++;
                ax = bx;
                ay = by;
            }
        }

        px = cmd->x;
        py = cmd->y;
    }

    box_msdf_shape_finish(&shape);
    uint8_t *pixels = ecs_os_malloc_n(uint8_t, bytes);
    box_msdf_generate(&shape, &raster, pixels);
    box_msdf_shape_fini(&shape);

    // Far texels saturate to their side; near texels have the median and the true distance on
    // the side the nonzero fill gives, except within half a texel of the outline
    int32_t far = 0, near = 0, wrong_far = 0, wrong_median = 0, wrong_alpha = 0;
    for (y = 0; y < raster.height; y ++) {
        for (x = 0; x < raster.width; x ++) {
            double cx = raster.left + (x + 0.5) / raster.scale;
            double cy = raster.top - (y + 0.5) / raster.scale;
            double dist = 1e30;
            int32_t winding = 0;
            for (s = 0; s < segs; s ++) {
                double ax = seg[s][0], ay = seg[s][1], dx = seg[s][2] - ax, dy = seg[s][3] - ay;
                double len2 = dx * dx + dy * dy;
                double t = len2 > 0.0 ? ((cx - ax) * dx + (cy - ay) * dy) / len2 : 0.0;
                t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
                double ex = ax + t * dx - cx, ey = ay + t * dy - cy;
                dist = fmin(dist, sqrt(ex * ex + ey * ey));
                if ((ay <= cy) != (seg[s][3] <= cy)) {
                    double xc = ax + (cy - ay) / dy * dx;
                    if (xc > cx) {
                        winding += dy > 0.0 ? 1 : -1;
                    }
                }
            }

            bool inside = winding != 0;
            const uint8_t *t = &pixels[(y * raster.width + x) * 4];
            dist *= raster.scale;
            if (dist > raster.range) {
                far ++;
                for (c = 0; c < 4; c ++) {
                    wrong_far += t[c] != (inside ? 255 : 0);
                }

                continue;
            }

            if (dist < 0.5) {
                continue;
            }

            near ++;
            int32_t lo = t[0] < t[1] ? t[0] : t[1], hi = t[0] < t[1] ? t[1] : t[0];
            int32_t median = hi < t[2] ? hi : (lo > t[2] ? lo : t[2]);
            wrong_median += (median > 127) != inside;
            wrong_alpha += (t[3] > 127) != inside;
        }
    }

    test_assert(far > 0);
    test_assert(near > 0);
    test_int(wrong_far, 0);
    test_int(wrong_median, 0);
    test_int(wrong_alpha, 0);

    // The whole field matches the reference
    char path[4096];
    char *dir = box_app_locate("test/assets/msdf");
    snprintf(path, sizeof(path), "%s/%s.rgba", dir, name);
    ecs_os_free(dir);
    if (getenv("BOX_MSDF_WRITE_REFERENCE")) {
        FILE *w = fopen(path, "wb");
        test_assert(w != NULL);
        test_int((int32_t)fwrite(pixels, 1, (size_t)bytes, w), bytes);
        fclose(w);
    }

    FILE *f = fopen(path, "rb");
    test_assert(f != NULL);
    uint8_t *ref = ecs_os_malloc_n(uint8_t, bytes + 1);
    test_int((int32_t)fread(ref, 1, (size_t)bytes + 1, f), bytes);
    fclose(f);
    int32_t differ = 0;
    for (i = 0; i < bytes; i ++) {
        differ += abs((int32_t)pixels[i] - (int32_t)ref[i]) > MSDF_TOLERANCE;
    }

    test_int(differ, 0);
    ecs_os_free(ref);
    ecs_os_free(pixels);
}

/** A square with a quadratic curve that bulges out on the left and one that bends in on the
 * right. */
void Msdf_curves(void) {
    const char *name = "curves";
    const msdf_cmd_t cmds[] = {
        { MSDF_MOVE, 0, 0, 8, 8 },
        { MSDF_QUAD, 0, 20, 8, 32 },
        { MSDF_LINE, 0, 0, 32, 32 },
        { MSDF_QUAD, 20, 20, 32, 8 },
    };

    int32_t i, s, x, y, c, count = (int32_t)(sizeof(cmds) / sizeof(cmds[0]));
    box_msdf_raster_t raster = {
        .scale = 1.0, .left = 0.0, .top = 40.0, .width = 40, .height = 40, .range = 4.0
    };
    int32_t bytes = raster.width * raster.height * 4;

    // Build the shape, and the outline flattened into lines for the expected distance and fill
    box_msdf_shape_t shape;
    box_msdf_shape_init(&shape);
    double seg[MSDF_SEGMENTS][4];
    int32_t segs = 0;
    double px = 0.0, py = 0.0, sx = 0.0, sy = 0.0;
    for (i = 0; i <= count; i ++) {
        if ((i == count || cmds[i].op == MSDF_MOVE) && i > 0 && (px != sx || py != sy)) {
            test_assert(segs < MSDF_SEGMENTS);
            seg[segs][0] = px; seg[segs][1] = py; seg[segs][2] = sx; seg[segs][3] = sy;
            segs ++;
        }

        if (i == count) {
            break;
        }

        const msdf_cmd_t *cmd = &cmds[i];
        if (cmd->op == MSDF_MOVE) {
            box_msdf_move_to(&shape, cmd->x, cmd->y);
            sx = cmd->x;
            sy = cmd->y;
        } else if (cmd->op == MSDF_LINE) {
            box_msdf_line_to(&shape, cmd->x, cmd->y);
            test_assert(segs < MSDF_SEGMENTS);
            seg[segs][0] = px; seg[segs][1] = py; seg[segs][2] = cmd->x; seg[segs][3] = cmd->y;
            segs ++;
        } else {
            box_msdf_quad_to(&shape, cmd->cx, cmd->cy, cmd->x, cmd->y);
            double ax = px, ay = py;
            for (s = 1; s <= MSDF_FLATTEN; s ++) {
                double t = (double)s / MSDF_FLATTEN, u = 1.0 - t;
                double bx = u * u * px + 2.0 * u * t * cmd->cx + t * t * cmd->x;
                double by = u * u * py + 2.0 * u * t * cmd->cy + t * t * cmd->y;
                test_assert(segs < MSDF_SEGMENTS);
                seg[segs][0] = ax; seg[segs][1] = ay; seg[segs][2] = bx; seg[segs][3] = by;
                segs ++;
                ax = bx;
                ay = by;
            }
        }

        px = cmd->x;
        py = cmd->y;
    }

    box_msdf_shape_finish(&shape);
    uint8_t *pixels = ecs_os_malloc_n(uint8_t, bytes);
    box_msdf_generate(&shape, &raster, pixels);
    box_msdf_shape_fini(&shape);

    // Far texels saturate to their side; near texels have the median and the true distance on
    // the side the nonzero fill gives, except within half a texel of the outline
    int32_t far = 0, near = 0, wrong_far = 0, wrong_median = 0, wrong_alpha = 0;
    for (y = 0; y < raster.height; y ++) {
        for (x = 0; x < raster.width; x ++) {
            double cx = raster.left + (x + 0.5) / raster.scale;
            double cy = raster.top - (y + 0.5) / raster.scale;
            double dist = 1e30;
            int32_t winding = 0;
            for (s = 0; s < segs; s ++) {
                double ax = seg[s][0], ay = seg[s][1], dx = seg[s][2] - ax, dy = seg[s][3] - ay;
                double len2 = dx * dx + dy * dy;
                double t = len2 > 0.0 ? ((cx - ax) * dx + (cy - ay) * dy) / len2 : 0.0;
                t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
                double ex = ax + t * dx - cx, ey = ay + t * dy - cy;
                dist = fmin(dist, sqrt(ex * ex + ey * ey));
                if ((ay <= cy) != (seg[s][3] <= cy)) {
                    double xc = ax + (cy - ay) / dy * dx;
                    if (xc > cx) {
                        winding += dy > 0.0 ? 1 : -1;
                    }
                }
            }

            bool inside = winding != 0;
            const uint8_t *t = &pixels[(y * raster.width + x) * 4];
            dist *= raster.scale;
            if (dist > raster.range) {
                far ++;
                for (c = 0; c < 4; c ++) {
                    wrong_far += t[c] != (inside ? 255 : 0);
                }

                continue;
            }

            if (dist < 0.5) {
                continue;
            }

            near ++;
            int32_t lo = t[0] < t[1] ? t[0] : t[1], hi = t[0] < t[1] ? t[1] : t[0];
            int32_t median = hi < t[2] ? hi : (lo > t[2] ? lo : t[2]);
            wrong_median += (median > 127) != inside;
            wrong_alpha += (t[3] > 127) != inside;
        }
    }

    test_assert(far > 0);
    test_assert(near > 0);
    test_int(wrong_far, 0);
    test_int(wrong_median, 0);
    test_int(wrong_alpha, 0);

    // The whole field matches the reference
    char path[4096];
    char *dir = box_app_locate("test/assets/msdf");
    snprintf(path, sizeof(path), "%s/%s.rgba", dir, name);
    ecs_os_free(dir);
    if (getenv("BOX_MSDF_WRITE_REFERENCE")) {
        FILE *w = fopen(path, "wb");
        test_assert(w != NULL);
        test_int((int32_t)fwrite(pixels, 1, (size_t)bytes, w), bytes);
        fclose(w);
    }

    FILE *f = fopen(path, "rb");
    test_assert(f != NULL);
    uint8_t *ref = ecs_os_malloc_n(uint8_t, bytes + 1);
    test_int((int32_t)fread(ref, 1, (size_t)bytes + 1, f), bytes);
    fclose(f);
    int32_t differ = 0;
    for (i = 0; i < bytes; i ++) {
        differ += abs((int32_t)pixels[i] - (int32_t)ref[i]) > MSDF_TOLERANCE;
    }

    test_int(differ, 0);
    ecs_os_free(ref);
    ecs_os_free(pixels);
}

/** The "K" of the default font (B612) as TextField.sharp_corners_large renders it: 2000 units per
 * em at BOX_TEXT_EM texels per em, in the area the atlas gives the glyph. The arms meet the stem
 * at a reflex corner (463, 802) that lies close to a texel row of the field. Where the outline
 * is at least a quarter texel away, the median of the field interpolated between the texels the
 * way a linear sampler does is on the side of the outline by at least a sixteenth of a texel: at
 * the 8 pixels per texel of the GPU test, a pixel 2 pixels from the edge is fully covered or
 * fully uncovered. Where it is at least a 32nd of a texel away, the median is on its side, so
 * the notches between the arms and the stem keep their tips. */
void Msdf_reflex_corner(void) {
    const double outline[][2] = {
        {190, 0}, {190, 1290}, {160, 1500}, {400, 1520}, {400, 856}, {941, 1500}, {1163, 1500},
        {632, 865}, {463, 802}, {637, 744}, {1190, 0}, {953, 0}, {400, 746}, {400, 0}
    };

    int32_t i, s, x, y, c, count = (int32_t)(sizeof(outline) / sizeof(outline[0]));
    box_msdf_raster_t raster = {
        .scale = 40.0 / 2000.0, .left = 0.0, .top = 1700.0, .width = 27, .height = 37,
        .range = 4.0
    };
    int32_t bytes = raster.width * raster.height * 4;

    box_msdf_shape_t shape;
    box_msdf_shape_init(&shape);
    box_msdf_move_to(&shape, outline[0][0], outline[0][1]);
    for (i = 1; i <= count; i ++) {
        box_msdf_line_to(&shape, outline[i % count][0], outline[i % count][1]);
    }

    box_msdf_shape_finish(&shape);
    uint8_t *pixels = ecs_os_malloc_n(uint8_t, bytes);
    box_msdf_generate(&shape, &raster, pixels);
    box_msdf_shape_fini(&shape);

    // Interpolate the field at 32 by 32 points per texel between the texel centers
    int32_t checked = 0, wrong = 0, sharp = 0;
    for (y = 0; y < (raster.height - 1) * 32; y ++) {
        for (x = 0; x < (raster.width - 1) * 32; x ++) {
            double u = 0.5 + (x + 0.5) / 32.0, v = 0.5 + (y + 0.5) / 32.0;
            double cx = raster.left + u / raster.scale, cy = raster.top - v / raster.scale;
            double dist = 1e30;
            bool inside = false;
            for (s = 0; s < count; s ++) {
                double ax = outline[s][0], ay = outline[s][1];
                double dx = outline[(s + 1) % count][0] - ax;
                double dy = outline[(s + 1) % count][1] - ay;
                double t = ((cx - ax) * dx + (cy - ay) * dy) / (dx * dx + dy * dy);
                t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
                double ex = ax + t * dx - cx, ey = ay + t * dy - cy;
                dist = fmin(dist, sqrt(ex * ex + ey * ey));
                if ((ay > cy) != (ay + dy > cy) && ax + (cy - ay) * dx / dy > cx) {
                    inside = !inside;
                }
            }

            if (dist * raster.scale < 1.0 / 32.0) {
                continue;
            }

            int32_t tx = (int32_t)floor(u - 0.5), ty = (int32_t)floor(v - 0.5);
            double fx = u - 0.5 - tx, fy = v - 0.5 - ty, ch[3];
            for (c = 0; c < 3; c ++) {
                const uint8_t *t = &pixels[(ty * raster.width + tx) * 4 + c];
                const uint8_t *b = t + raster.width * 4;
                ch[c] = ((t[0] * (1.0 - fx) + t[4] * fx) * (1.0 - fy) +
                    (b[0] * (1.0 - fx) + b[4] * fx) * fy) / 255.0;
            }

            double median = fmax(fmin(ch[0], ch[1]), fmin(fmax(ch[0], ch[1]), ch[2]));
            double field = (median - 0.5) * raster.range;
            sharp += inside ? field <= 0.0 : field >= 0.0;
            if (dist * raster.scale >= 0.25) {
                checked ++;
                wrong += inside ? field < 1.0 / 16.0 : field > -1.0 / 16.0;
            }
        }
    }

    test_assert(checked > 0);
    test_int(wrong, 0);
    test_int(sharp, 0);
    ecs_os_free(pixels);
}

/** More outlines with reflex and acute corners, checked like Msdf_reflex_corner: "M", "N", "W",
 * "X" and "k" at BOX_TEXT_EM texels per 2000 units, and an acute "V" notch cut into a block at
 * 16 texels per 2000 units, where the tip of the notch is a fraction of a texel wide. The arms
 * of the "k" meet in a notch next to the stem, between the notches at the stem, as those of the
 * "K" of Msdf_reflex_corner do. The clash of the channels
 * shows where a corner lies close to a texel row or column, so each outline is placed with one
 * of its corners a small fraction of a texel off the texel centers or off the middle between
 * them, at several such offsets. Where the outline is at least a quarter texel away, the
 * interpolated median is on its side by at least a sixteenth of a texel; where it is at least a
 * 32nd of a texel away, the median is on its side. */
void Msdf_corner_shapes(void) {
    const struct {
        const char *name;
        double scale;
        double corner[2];
        int32_t count;
        double points[16][2];
    } shapes[] = {
        {"M", 40.0 / 2000.0, {700, 500}, 13, {
            {150, 0}, {150, 1500}, {400, 1500}, {700, 500}, {1000, 1500}, {1250, 1500},
            {1250, 0}, {1060, 0}, {1060, 900}, {780, 250}, {620, 250}, {340, 900}, {340, 0}
        }},
        {"N", 40.0 / 2000.0, {1040, 380}, 10, {
            {150, 0}, {150, 1500}, {370, 1500}, {1040, 380}, {1040, 1500}, {1230, 1500},
            {1230, 0}, {1010, 0}, {340, 1120}, {340, 0}
        }},
        {"W", 40.0 / 2000.0, {580, 330}, 13, {
            {50, 1500}, {310, 1500}, {580, 330}, {880, 1500}, {1120, 1500}, {1420, 330},
            {1690, 1500}, {1950, 1500}, {1530, 0}, {1300, 0}, {1000, 1100}, {700, 0}, {470, 0}
        }},
        {"X", 40.0 / 2000.0, {560, 760}, 12, {
            {60, 0}, {560, 760}, {80, 1500}, {310, 1500}, {650, 850}, {990, 1500},
            {1220, 1500}, {740, 760}, {1240, 0}, {1010, 0}, {650, 670}, {290, 0}
        }},
        {"k", 40.0 / 2000.0, {400, 640}, 13, {
            {150, 0}, {150, 1600}, {340, 1600}, {340, 690}, {760, 1100}, {990, 1100},
            {560, 700}, {400, 640}, {570, 580}, {1010, 0}, {790, 0}, {340, 590}, {340, 0}
        }},
        {"V", 16.0 / 2000.0, {600, 700}, 7, {
            {0, 0}, {0, 1500}, {400, 1500}, {600, 700}, {800, 1500}, {1200, 1500}, {1200, 0}
        }}
    };

    // Fraction of a texel at which the chosen corner lands, in x and y
    const double offsets[][2] = {
        {13.0 / 32.0, 5.0 / 32.0}, {1.0 / 32.0, 15.0 / 32.0},
        {31.0 / 32.0, 17.0 / 32.0}, {15.0 / 32.0, 31.0 / 32.0},
        {17.0 / 32.0, 17.0 / 32.0}, {13.0 / 32.0, 1.0 / 32.0}, {9.0 / 32.0, 29.0 / 32.0}
    };

    int32_t shape_count = (int32_t)(sizeof(shapes) / sizeof(shapes[0]));
    int32_t offset_count = (int32_t)(sizeof(offsets) / sizeof(offsets[0]));
    int32_t n, o, i, s, x, y, c, checked = 0, wrong = 0, sharp = 0;
    for (n = 0; n < shape_count; n ++) {
        const double (*outline)[2] = shapes[n].points;
        int32_t count = shapes[n].count;
        double scale = shapes[n].scale, pad = 3.0;
        double minx = 1e30, maxx = -1e30, miny = 1e30, maxy = -1e30;
        for (i = 0; i < count; i ++) {
            minx = fmin(minx, outline[i][0]);
            maxx = fmax(maxx, outline[i][0]);
            miny = fmin(miny, outline[i][1]);
            maxy = fmax(maxy, outline[i][1]);
        }

        for (o = 0; o < offset_count; o ++) {
            // Place the corner at the offset from a texel boundary of the field
            double kx = shapes[n].corner[0], ky = shapes[n].corner[1];
            double left = kx - (floor((kx - minx) * scale + pad) + offsets[o][0]) / scale;
            double top = ky + (floor((maxy - ky) * scale + pad) + offsets[o][1]) / scale;
            box_msdf_raster_t raster = {
                .scale = scale, .left = left, .top = top,
                .width = (int32_t)ceil((maxx - left) * scale + pad),
                .height = (int32_t)ceil((top - miny) * scale + pad),
                .range = 4.0
            };
            int32_t bytes = raster.width * raster.height * 4;

            box_msdf_shape_t shape;
            box_msdf_shape_init(&shape);
            box_msdf_move_to(&shape, outline[0][0], outline[0][1]);
            for (i = 1; i <= count; i ++) {
                box_msdf_line_to(&shape, outline[i % count][0], outline[i % count][1]);
            }

            box_msdf_shape_finish(&shape);
            uint8_t *pixels = ecs_os_malloc_n(uint8_t, bytes);
            box_msdf_generate(&shape, &raster, pixels);
            box_msdf_shape_fini(&shape);

            // Interpolate the field at 32 by 32 points per texel between the texel centers
            int32_t case_checked = 0, case_wrong = 0, case_sharp = 0;
            for (y = 0; y < (raster.height - 1) * 32; y ++) {
                for (x = 0; x < (raster.width - 1) * 32; x ++) {
                    double u = 0.5 + (x + 0.5) / 32.0, v = 0.5 + (y + 0.5) / 32.0;
                    double cx = raster.left + u / raster.scale;
                    double cy = raster.top - v / raster.scale;
                    double dist = 1e30;
                    bool inside = false;
                    for (s = 0; s < count; s ++) {
                        double ax = outline[s][0], ay = outline[s][1];
                        double dx = outline[(s + 1) % count][0] - ax;
                        double dy = outline[(s + 1) % count][1] - ay;
                        double t = ((cx - ax) * dx + (cy - ay) * dy) / (dx * dx + dy * dy);
                        t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
                        double ex = ax + t * dx - cx, ey = ay + t * dy - cy;
                        dist = fmin(dist, sqrt(ex * ex + ey * ey));
                        if ((ay > cy) != (ay + dy > cy) && ax + (cy - ay) * dx / dy > cx) {
                            inside = !inside;
                        }
                    }

                    if (dist * raster.scale < 1.0 / 32.0) {
                        continue;
                    }

                    int32_t tx = (int32_t)floor(u - 0.5), ty = (int32_t)floor(v - 0.5);
                    double fx = u - 0.5 - tx, fy = v - 0.5 - ty, ch[3];
                    for (c = 0; c < 3; c ++) {
                        const uint8_t *t = &pixels[(ty * raster.width + tx) * 4 + c];
                        const uint8_t *b = t + raster.width * 4;
                        ch[c] = ((t[0] * (1.0 - fx) + t[4] * fx) * (1.0 - fy) +
                            (b[0] * (1.0 - fx) + b[4] * fx) * fy) / 255.0;
                    }

                    double median = fmax(fmin(ch[0], ch[1]), fmin(fmax(ch[0], ch[1]), ch[2]));
                    double field = (median - 0.5) * raster.range;
                    case_sharp += inside ? field <= 0.0 : field >= 0.0;
                    if (dist * raster.scale >= 0.25) {
                        case_checked ++;
                        case_wrong += inside ? field < 1.0 / 16.0 : field > -1.0 / 16.0;
                    }
                }
            }

            if (!case_checked || case_wrong || case_sharp) {
                printf("Msdf corner shape %s at offset (%g, %g): checked %d, wrong %d, sharp %d\n",
                    shapes[n].name, offsets[o][0], offsets[o][1],
                    case_checked, case_wrong, case_sharp);
            }

            test_assert(case_checked > 0);
            checked += case_checked;
            wrong += case_wrong;
            sharp += case_sharp;
            ecs_os_free(pixels);
        }
    }

    test_assert(checked > 0);
    test_int(wrong, 0);
    test_int(sharp, 0);
}
