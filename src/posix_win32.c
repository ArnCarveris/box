/**
 * @file posix_win32.c
 * @brief The POSIX calls of posix.h that the C runtime of Windows doesn't have.
 */

#ifdef _WIN32

#include "posix.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/utime.h>

/** Deepest folder level box_posix_rmtree descends into */
#define BOX_POSIX_RMTREE_DEPTH (64)

/** The reparse data of a junction, which the headers of Windows declare for drivers only */
typedef struct box_posix_mount_point_t {
    DWORD tag;                      /**< IO_REPARSE_TAG_MOUNT_POINT */
    WORD data_size;                 /**< Bytes from substitute_offset to the end of names */
    WORD reserved;                  /**< Zero */
    WORD substitute_offset;         /**< Byte offset in names of the \??\ path */
    WORD substitute_size;           /**< Bytes of the \??\ path, without its terminator */
    WORD print_offset;              /**< Byte offset in names of the path shown to users */
    WORD print_size;                /**< Bytes of the path shown, without its terminator */
    WCHAR names[2 * MAX_PATH + 8];  /**< Both paths, each with a terminator */
} box_posix_mount_point_t;

/** Convert a FILETIME of 100 nanosecond ticks to seconds and microseconds.
 *
 * @param ft The time
 * @param tv Receives the time
 */
static void box_posix_filetime(
    const FILETIME *ft,
    struct box_posix_timeval *tv)
{
    ULONGLONG ticks = ((ULONGLONG)ft->dwHighDateTime << 32) | ft->dwLowDateTime;
    tv->tv_sec = (long)(ticks / 10000000);
    tv->tv_usec = (long)((ticks % 10000000) / 10);
}

char* box_posix_getcwd(
    char *buffer,
    size_t size)
{
    char *result = _getcwd(buffer, (int)size);
    char *ch = result;
    while (ch && ch[0]) {
        if (ch[0] == '\\') {
            ch[0] = '/';
        }

        ch ++;
    }

    return result;
}

long sysconf(
    int name)
{
    long result = -1;
    if (name == _SC_NPROCESSORS_ONLN) {
        result = (long)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    }

    return result;
}

int getloadavg(
    double *loadavg,
    int nelem)
{
    (void)loadavg;
    (void)nelem;

    return -1;
}

void box_posix_rmtree(
    const char *path,
    int depth)
{
    WIN32_FIND_DATAA entry;
    char pattern[MAX_PATH], child[MAX_PATH];
    HANDLE find = INVALID_HANDLE_VALUE;
    DWORD attributes = GetFileAttributesA(path);
    bool link = attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
    if (!link && snprintf(pattern, sizeof(pattern), "%s/*", path) < (int)sizeof(pattern)) {
        find = FindFirstFileA(pattern, &entry);
    }

    if (find == INVALID_HANDLE_VALUE) {
        goto done;
    }

    do {
        bool dir = entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
        if (!strcmp(entry.cFileName, ".") || !strcmp(entry.cFileName, "..") ||
            snprintf(child, sizeof(child), "%s/%s", path, entry.cFileName) >= (int)sizeof(child))
        {
            continue;
        }

        if (!dir) {
            DeleteFileA(child);
        } else if (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            RemoveDirectoryA(child);
        } else if (depth < BOX_POSIX_RMTREE_DEPTH) {
            box_posix_rmtree(child, depth + 1);
        }
    } while (FindNextFileA(find, &entry));

    FindClose(find);
done:
    RemoveDirectoryA(path);
}

int box_posix_lstat(
    const char *path,
    struct stat *st)
{
    DWORD attributes = GetFileAttributesA(path);
    int result = -1;
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        goto done;
    }

    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        memset(st, 0, sizeof(*st));
        st->st_mode = S_IFLNK | 0777;
        result = 0;
    } else {
        result = stat(path, st);
    }

done:
    return result;
}

int box_posix_rename(
    const char *from,
    const char *to)
{
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
}

int box_posix_unlink(
    const char *path)
{
    DWORD attributes = GetFileAttributesA(path);
    int result = -1;
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        goto done;
    }

    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        result = RemoveDirectoryA(path) ? 0 : -1;
    } else {
        result = _unlink(path);
    }

done:
    return result;
}

/** Create a junction, the link to a folder that needs no right to create links.
 *
 * @param target The folder the junction points to
 * @param path The junction
 * @return 0 on success, -1 otherwise
 */
static int box_posix_junction(
    const char *target,
    const char *path)
{
    box_posix_mount_point_t data = {0};
    char full[MAX_PATH];
    WCHAR wide[MAX_PATH];
    WCHAR *names = data.names;
    HANDLE link = INVALID_HANDLE_VALUE;
    DWORD bytes = 0, size = GetFullPathNameA(target, MAX_PATH, full, NULL);
    int length = 0, result = -1;
    if (size && size < MAX_PATH) {
        length = MultiByteToWideChar(CP_ACP, 0, full, -1, wide, MAX_PATH) - 1;
    }

    if (length <= 0 || !CreateDirectoryA(path, NULL)) {
        goto done;
    }

    memcpy(names, L"\\??\\", 4 * sizeof(WCHAR));
    memcpy(names + 4, wide, length * sizeof(WCHAR));
    memcpy(names + 5 + length, wide, length * sizeof(WCHAR));
    data.tag = IO_REPARSE_TAG_MOUNT_POINT;
    data.substitute_size = (WORD)((4 + length) * sizeof(WCHAR));
    data.print_offset = (WORD)(data.substitute_size + sizeof(WCHAR));
    data.print_size = (WORD)(length * sizeof(WCHAR));
    data.data_size = (WORD)(8 + data.print_offset + data.print_size + sizeof(WCHAR));
    link = CreateFileA(path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (link != INVALID_HANDLE_VALUE) {
        if (DeviceIoControl(link, FSCTL_SET_REPARSE_POINT, &data, data.data_size + 8,
            NULL, 0, &bytes, NULL))
        {
            result = 0;
        }

        CloseHandle(link);
    }

    if (result) {
        RemoveDirectoryA(path);
    }

done:
    return result;
}

int symlink(
    const char *target,
    const char *path)
{
    DWORD attributes = GetFileAttributesA(target);
    DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
    bool dir = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
    int result = 0;
    if (dir) {
        flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
    }

    if (!CreateSymbolicLinkA(path, target, flags)) {
        result = dir ? box_posix_junction(target, path) : -1;
    }

    return result;
}

int utimensat(
    int dirfd,
    const char *path,
    const struct timespec times[2],
    int flags)
{
    struct _utimbuf buf;
    int result = -1;
    (void)flags;
    if (dirfd != AT_FDCWD) {
        goto done;
    }

    buf.actime = times[0].tv_sec;
    buf.modtime = times[1].tv_sec;
    result = _utime(path, &buf);
done:
    return result;
}

char* mkdtemp(
    char *path)
{
    char *result = NULL;
    if (_mktemp_s(path, strlen(path) + 1) || _mkdir(path)) {
        goto done;
    }

    result = path;
done:
    return result;
}

int sched_yield(void)
{
    SwitchToThread();

    return 0;
}

int getrusage(
    int who,
    struct rusage *usage)
{
    FILETIME created, exited, kernel, user;
    int result = -1;
    if (who != RUSAGE_SELF ||
        !GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
    {
        goto done;
    }

    box_posix_filetime(&user, &usage->ru_utime);
    box_posix_filetime(&kernel, &usage->ru_stime);
    result = 0;
done:
    return result;
}

#endif
