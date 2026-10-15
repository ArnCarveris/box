/**
 * @file types/utf8.c
 * @brief UTF-8 decoding and encoding shared by input, the console and text.
 */

#include <box.h>

int32_t box_utf8_next(
    const char **str)
{
    const uint8_t *p = (const uint8_t*)*str;
    int32_t result = p[0], extra = 0, length = 1, i;
    uint8_t lower = 0x80, upper = 0xBF;
    if (p[0] >= 0xC2 && p[0] <= 0xDF) {
        result = p[0] & 0x1F;
        extra = 1;
    } else if (p[0] >= 0xE0 && p[0] <= 0xEF) {
        result = p[0] & 0x0F;
        extra = 2;
        lower = p[0] == 0xE0 ? 0xA0 : 0x80;
        upper = p[0] == 0xED ? 0x9F : 0xBF;
    } else if (p[0] >= 0xF0 && p[0] <= 0xF4) {
        result = p[0] & 0x07;
        extra = 3;
        lower = p[0] == 0xF0 ? 0x90 : 0x80;
        upper = p[0] == 0xF4 ? 0x8F : 0xBF;
    } else if (p[0] >= 0x80) {
        result = BOX_UTF8_REPLACEMENT;
    }

    for (i = 1; i <= extra && result != BOX_UTF8_REPLACEMENT; i ++) {
        if (p[i] < lower || p[i] > upper) {
            result = BOX_UTF8_REPLACEMENT;
        } else {
            result = (result << 6) | (p[i] & 0x3F);
            lower = 0x80;
            upper = 0xBF;
            length = i + 1;
        }
    }

    *str = (const char*)(p + length);

    return result;
}

int32_t box_utf8_encode(
    int32_t code,
    char *out)
{
    int32_t size = 0;
    if (code < 0 || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
        code = BOX_UTF8_REPLACEMENT;
    }

    if (code < 0x80) {
        out[size ++] = (char)code;
    } else if (code < 0x800) {
        out[size ++] = (char)(0xC0 | (code >> 6));
        out[size ++] = (char)(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out[size ++] = (char)(0xE0 | (code >> 12));
        out[size ++] = (char)(0x80 | ((code >> 6) & 0x3F));
        out[size ++] = (char)(0x80 | (code & 0x3F));
    } else {
        out[size ++] = (char)(0xF0 | (code >> 18));
        out[size ++] = (char)(0x80 | ((code >> 12) & 0x3F));
        out[size ++] = (char)(0x80 | ((code >> 6) & 0x3F));
        out[size ++] = (char)(0x80 | (code & 0x3F));
    }

    return size;
}
