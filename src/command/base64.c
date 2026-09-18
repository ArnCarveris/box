/**
 * @file command/base64.c
 * @brief Base64 encoder for the binary replies of the request export.
 */

#include "command_internal.h"

/** Alphabet of the standard base64 encoding */
static const char box_base64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char* box_base64_encode(
    const void *data,
    int32_t size)
{
    ecs_assert(size >= 0, ECS_INVALID_PARAMETER, "negative size");
    const unsigned char *in = data;
    int32_t blocks = (size + 2) / 3;
    char *result = ecs_os_malloc(blocks * 4 + 1);
    char *out = result;
    int32_t i;
    for (i = 0; i < size; i += 3) {
        int32_t left = size - i;
        uint32_t bits = (uint32_t)in[i] << 16;
        if (left > 1) {
            bits |= (uint32_t)in[i + 1] << 8;
        }

        if (left > 2) {
            bits |= (uint32_t)in[i + 2];
        }

        out[0] = box_base64_alphabet[(bits >> 18) & 63];
        out[1] = box_base64_alphabet[(bits >> 12) & 63];
        out[2] = left > 1 ? box_base64_alphabet[(bits >> 6) & 63] : '=';
        out[3] = left > 2 ? box_base64_alphabet[bits & 63] : '=';
        out += 4;
    }

    out[0] = '\0';

    return result;
}
