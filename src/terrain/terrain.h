/**
 * @file terrain/terrain.h
 * @brief The grid image of a TileTerrain, shared by box.terrain and the render extract.
 */

#ifndef BOX_TERRAIN_INTERNAL_H
#define BOX_TERRAIN_INTERNAL_H

#include <box.h>

/** The image the grid of a TileTerrain makes, on the terrain entity, set by the engine */
typedef struct TileTerrainGrid {
    ecs_entity_t texture;   /**< Child texture entity with the TexturePixels of the grid: an R8
                             *   pixel per tile with the index of its kind, rows from -z */
    int32_t columns;        /**< Tiles along x */
    int32_t rows;           /**< Tiles along z */
} TileTerrainGrid;

/** Component id of TileTerrainGrid */
extern ECS_COMPONENT_DECLARE(TileTerrainGrid);

/** Count the tiles of the grid of a TileTerrain.
 *
 * @param tiles The grid text, NULL for none
 * @param columns Receives the symbols of the longest row, at least 1
 * @param rows Receives the rows with a symbol, at least 1
 */
void box_terrain_dims(
    const char *tiles,
    int32_t *columns,
    int32_t *rows);

#endif
