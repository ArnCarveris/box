/**
 * @file platform/win32/box_win32.h
 * @brief POSIX names used by box, mapped onto the MSVC runtime and Win32.
 *
 * Force included into every box source on Windows (see project.json).
 *
 * The other headers of this folder stand in for POSIX headers MSVC lacks.
 * None of them include windows.h, which keeps the namespace of a source clean.
 */

#ifndef BOX_WIN32_H
#define BOX_WIN32_H

#include <direct.h>
#include <io.h>
#include <process.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX (1024)
#endif

#define F_OK (0)
#define W_OK (2)
#define R_OK (4)

#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif

#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif

#define _SC_NPROCESSORS_ONLN (84)

#define mkdir(path, mode) _mkdir(path)
#define lstat(path, st) stat(path, st)
#define realpath(path, resolved) box_win32_realpath(path, resolved)
#define sysconf(name) box_win32_sysconf(name)
#define getcwd(buf, size) box_win32_getcwd(buf, size)

/** Signed size type of POSIX */
typedef intptr_t ssize_t;

/** Resolve a path to an absolute path with forward slashes.
 *
 * @param path The path to resolve
 * @param resolved Receives the result, PATH_MAX bytes
 * @return resolved on success, NULL when the path does not exist
 */
char* box_win32_realpath(
    const char *path,
    char *resolved);

/** Get the working directory with forward slashes.
 *
 * @param buf Receives the directory
 * @param size Bytes of the buffer
 * @return buf on success, NULL on failure
 */
char* box_win32_getcwd(
    char *buf,
    size_t size);

/** Get the system load average, which Windows does not track.
 *
 * @param loadavg Receives up to nelem samples
 * @param nelem Number of samples requested
 * @return -1, the load average is unavailable
 */
int getloadavg(
    double *loadavg,
    int nelem);

/** Query a system value.
 *
 * @param name Only _SC_NPROCESSORS_ONLN is supported
 * @return The value, or -1 for an unsupported name
 */
long box_win32_sysconf(
    int name);

/** Directory of the running executable, with forward slashes.
 *
 * @param out Receives the folder, empty when it is unknown
 * @param size Bytes of the output buffer
 */
void box_win32_exe_dir(
    char *out,
    size_t size);

/** Folder for per-process temporary files, with forward slashes.
 *
 * @param out Receives the folder without a trailing slash
 * @param size Bytes of the output buffer
 */
void box_win32_temp_dir(
    char *out,
    size_t size);

#endif
