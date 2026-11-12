/**
 * @file posix.h
 * @brief The POSIX calls box uses: unistd.h, or the C runtime and posix_win32.c on Windows.
 */

#ifndef BOX_POSIX_H
#define BOX_POSIX_H

#ifdef _WIN32

#include <direct.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <stdlib.h>
#include <time.h>

#define F_OK (0)
#define R_OK (4)
#define STDERR_FILENO (2)
#define _SC_NPROCESSORS_ONLN (84)
#define RUSAGE_SELF (0)
#define AT_FDCWD (-100)

#ifndef PATH_MAX
#define PATH_MAX _MAX_PATH
#endif

#define access _access
#define chdir _chdir
#define getcwd box_posix_getcwd
#define getpid _getpid
#define isatty _isatty
#define lstat box_posix_lstat
#define rename box_posix_rename
#define rmdir _rmdir
#define unlink box_posix_unlink
#define mkdir(path, mode) _mkdir(path)
#define realpath(path, resolved) _fullpath(resolved, path, PATH_MAX)

#ifndef S_IFLNK
#define S_IFLNK (0xA000)
#endif

#ifndef S_ISLNK
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#endif

#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif

#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif

/** Whether a path is absolute: rooted at a slash, a backslash or a drive */
#define BOX_POSIX_ABSOLUTE(path) \
    ((path)[0] == '/' || (path)[0] == '\\' || ((path)[0] && (path)[1] == ':'))

/** Seconds and microseconds, as getrusage reports them */
struct box_posix_timeval {
    long tv_sec;    /**< Seconds */
    long tv_usec;   /**< Microseconds */
};

/** CPU time of the process, as getrusage reports it */
struct rusage {
    struct box_posix_timeval ru_utime;  /**< User time */
    struct box_posix_timeval ru_stime;  /**< System time */
};

/** Get the working folder with forward slashes, as the callers split it at them.
 *
 * @param buffer Receives the folder
 * @param size Bytes of buffer
 * @return buffer, NULL when the folder does not fit or is unknown
 */
char* box_posix_getcwd(
    char *buffer,
    size_t size);

/** Get a configuration value of the system.
 *
 * @param name Only _SC_NPROCESSORS_ONLN
 * @return Number of logical cores, -1 for any other name
 */
long sysconf(
    int name);

/** Get the load average of the system, which Windows doesn't have.
 *
 * @param loadavg Unused
 * @param nelem Unused
 * @return -1
 */
int getloadavg(
    double *loadavg,
    int nelem);

/** Get the CPU time of the process.
 *
 * @param who Only RUSAGE_SELF
 * @param usage Receives the user and system time
 * @return 0 on success, -1 otherwise
 */
int getrusage(
    int who,
    struct rusage *usage);

/** Rename a file, replacing the file at the new name as POSIX rename does.
 *
 * @param from The file
 * @param to Its new name
 * @return 0 on success, -1 otherwise
 */
int box_posix_rename(
    const char *from,
    const char *to);

/** Remove a file, or a link to a folder, which Windows removes as a folder.
 *
 * @param path The file or link
 * @return 0 on success, -1 otherwise
 */
int box_posix_unlink(
    const char *path);

/** Create a symbolic link, or a junction to a folder without Developer Mode or the right to
 * create links.
 *
 * @param target What the link points to, a file or a folder
 * @param path The link
 * @return 0 on success, -1 otherwise
 */
int symlink(
    const char *target,
    const char *path);

/** Set the access and modification times of a file to whole seconds.
 *
 * @param dirfd Only AT_FDCWD
 * @param path The file
 * @param times The access time, then the modification time
 * @param flags Unused
 * @return 0 on success, -1 otherwise
 */
int utimensat(
    int dirfd,
    const char *path,
    const struct timespec times[2],
    int flags);

/** Create a folder with a unique name from a template that ends in XXXXXX.
 *
 * @param path The template, which receives the name
 * @return path, NULL when no folder was created
 */
char* mkdtemp(
    char *path);

/** Yield the rest of the time slice of the thread.
 *
 * @return 0
 */
int sched_yield(void);

/** Get the status of a file without following a link or junction, which reports S_IFLNK.
 *
 * @param path The file
 * @param st Receives the status
 * @return 0 on success, -1 otherwise
 */
int box_posix_lstat(
    const char *path,
    struct stat *st);

/** Remove a folder and everything in it without following links or junctions, the folder
 * itself included: a link is removed, not what it points to.
 *
 * @param path The folder
 * @param depth Folder levels above this one, 0 for the folder itself
 */
void box_posix_rmtree(
    const char *path,
    int depth);

#else

#include <unistd.h>

/** Whether a path is absolute */
#define BOX_POSIX_ABSOLUTE(path) ((path)[0] == '/')

#endif

#endif
