/**
 * @file scratch.h
 * @brief Paths of checkpoint test files that live outside the process folder.
 */

#ifndef CHECKPOINT_SCRATCH_H
#define CHECKPOINT_SCRATCH_H

#include <box.h>

/** Path of a file or folder in the temporary folder of the system.
 *
 * Allocates with the flecs OS API, so a world must exist.
 *
 * @param name Name of the file or folder
 * @return The path, freed with ecs_os_free
 */
static inline char* scratch_path(
    const char *name)
{
#ifdef _WIN32
    char tmp[PATH_MAX];
    box_win32_temp_dir(tmp, sizeof(tmp));
    char *result = flecs_asprintf("%s/%s", tmp, name);
#else
    char *result = flecs_asprintf("/tmp/%s", name);
#endif

    return result;
}

#endif
