/**
 * @file platform/win32/dirent.h
 * @brief Directory listing of POSIX, implemented with FindFirstFile.
 */

#ifndef BOX_WIN32_DIRENT_H
#define BOX_WIN32_DIRENT_H

#include "box_win32.h"

/** One entry of a directory listing */
struct dirent {
    char d_name[260]; /**< Name of the entry, without the folder */
};

/** An open directory listing */
typedef struct box_win32_dir_t DIR;

/** Open a directory listing.
 *
 * @param path The folder
 * @return The listing, or NULL when the folder cannot be read
 */
DIR* opendir(
    const char *path);

/** Read the next entry of a listing.
 *
 * @param dir The listing
 * @return The entry, valid until the next call, or NULL at the end
 */
struct dirent* readdir(
    DIR *dir);

/** Close a directory listing.
 *
 * @param dir The listing
 * @return 0
 */
int closedir(
    DIR *dir);

#endif
