/**
 * @file renderer/packet.h
 * @brief Frame packet exchanged between the main thread and the renderer.
 *
 * The packet is the only data the renderer sees; it never touches ECS state.
 */

#ifndef BOX_RENDERER_PACKET_H
#define BOX_RENDERER_PACKET_H

#include <stdint.h>
#include <stdbool.h>

/** Longest capture path stored in a packet */
#define BOX_CAPTURE_PATH_MAX (512)

/** All work the renderer performs for one frame */
typedef struct box_packet_t {
    uint32_t frame;                          /**< Frame number */
    double time;                             /**< Seconds since start */
    uint32_t width;                          /**< Target width in pixels */
    uint32_t height;                         /**< Target height in pixels */
    float clear[4];                          /**< Clear color, linear RGBA */
    char capture_path[BOX_CAPTURE_PATH_MAX]; /**< PNG to write, empty for none */
} box_packet_t;

#endif
