/**
 * @file platform/platform_win32.c
 * @brief POSIX threads, directory listing and process queries on Win32.
 */

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "win32/pthread.h"
#include "win32/dirent.h"
#include "win32/sys/resource.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/** A running thread */
struct box_win32_thread_t {
    HANDLE handle;      /**< Win32 thread handle */
    void *(*fn)(void*); /**< The thread function */
    void *arg;          /**< Argument of the thread function */
    void *result;       /**< Return value of the thread function */
};

/** An open directory listing */
struct box_win32_dir_t {
    HANDLE find;             /**< FindFirstFile handle */
    WIN32_FIND_DATAA data;   /**< The entry FindNextFile will replace */
    bool pending;            /**< data holds an entry readdir has not returned */
    struct dirent entry;     /**< The entry returned by readdir */
};

/** Replace every backslash of a path with a forward slash.
 *
 * @param path The path to rewrite in place
 */
static void box_win32_forward_slashes(
    char *path)
{
    for (; *path; path ++) {
        if (*path == '\\') {
            *path = '/';
        }
    }
}

/** Thread entry that runs the pthread function.
 *
 * @param arg The thread
 * @return 0
 */
static DWORD WINAPI box_win32_thread_main(
    LPVOID arg)
{
    struct box_win32_thread_t *t = arg;
    t->result = t->fn(t->arg);

    return 0;
}

int pthread_create(
    pthread_t *thread,
    const pthread_attr_t *attr,
    box_win32_thread_fn fn,
    void *arg)
{
    int result = -1;
    (void)attr;
    struct box_win32_thread_t *t = calloc(1, sizeof(struct box_win32_thread_t));
    t->fn = fn;
    t->arg = arg;
    t->handle = CreateThread(NULL, 0, box_win32_thread_main, t, 0, NULL);
    if (!t->handle) {
        free(t);
        goto done;
    }

    *thread = t;
    result = 0;
done:
    return result;
}

int pthread_join(
    pthread_t thread,
    void **result)
{
    WaitForSingleObject(thread->handle, INFINITE);
    CloseHandle(thread->handle);
    if (result) {
        *result = thread->result;
    }

    free(thread);

    return 0;
}

int pthread_mutex_init(
    pthread_mutex_t *m,
    const pthread_mutexattr_t *attr)
{
    (void)attr;
    InitializeSRWLock((PSRWLOCK)m);

    return 0;
}

int pthread_mutex_destroy(
    pthread_mutex_t *m)
{
    (void)m;

    return 0;
}

int pthread_mutex_lock(
    pthread_mutex_t *m)
{
    AcquireSRWLockExclusive((PSRWLOCK)m);

    return 0;
}

int pthread_mutex_unlock(
    pthread_mutex_t *m)
{
    ReleaseSRWLockExclusive((PSRWLOCK)m);

    return 0;
}

int pthread_cond_init(
    pthread_cond_t *c,
    const pthread_condattr_t *attr)
{
    (void)attr;
    InitializeConditionVariable((PCONDITION_VARIABLE)c);

    return 0;
}

int pthread_cond_destroy(
    pthread_cond_t *c)
{
    (void)c;

    return 0;
}

int pthread_cond_wait(
    pthread_cond_t *c,
    pthread_mutex_t *m)
{
    SleepConditionVariableSRW((PCONDITION_VARIABLE)c, (PSRWLOCK)m, INFINITE, 0);

    return 0;
}

int pthread_cond_signal(
    pthread_cond_t *c)
{
    WakeConditionVariable((PCONDITION_VARIABLE)c);

    return 0;
}

int pthread_cond_broadcast(
    pthread_cond_t *c)
{
    WakeAllConditionVariable((PCONDITION_VARIABLE)c);

    return 0;
}

DIR* opendir(
    const char *path)
{
    DIR *result = NULL;
    char pattern[MAX_PATH + 3];
    snprintf(pattern, sizeof(pattern), "%s/*", path);
    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) {
        goto done;
    }

    result = calloc(1, sizeof(DIR));
    result->find = find;
    result->data = data;
    result->pending = true;
done:
    return result;
}

struct dirent* readdir(
    DIR *dir)
{
    struct dirent *result = NULL;
    if (!dir->pending && !FindNextFileA(dir->find, &dir->data)) {
        goto done;
    }

    dir->pending = false;
    snprintf(dir->entry.d_name, sizeof(dir->entry.d_name), "%s", dir->data.cFileName);
    result = &dir->entry;
done:
    return result;
}

int closedir(
    DIR *dir)
{
    FindClose(dir->find);
    free(dir);

    return 0;
}

int getrusage(
    int who,
    struct rusage *usage)
{
    int result = -1;
    FILETIME created, exited, kernel, user;
    (void)who;
    memset(usage, 0, sizeof(*usage));
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        goto done;
    }

    ULONGLONG u = ((ULONGLONG)user.dwHighDateTime << 32 | user.dwLowDateTime) / 10;
    ULONGLONG k = ((ULONGLONG)kernel.dwHighDateTime << 32 | kernel.dwLowDateTime) / 10;
    usage->ru_utime.tv_sec = (long)(u / 1000000);
    usage->ru_utime.tv_usec = (long)(u % 1000000);
    usage->ru_stime.tv_sec = (long)(k / 1000000);
    usage->ru_stime.tv_usec = (long)(k % 1000000);
    result = 0;
done:
    return result;
}

char* box_win32_realpath(
    const char *path,
    char *resolved)
{
    char *result = NULL;
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES || !_fullpath(resolved, path, PATH_MAX)) {
        goto done;
    }

    box_win32_forward_slashes(resolved);
    result = resolved;
done:
    return result;
}

char* box_win32_getcwd(
    char *buf,
    size_t size)
{
    char *result = _getcwd(buf, (int)size);
    if (result) {
        box_win32_forward_slashes(result);
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

long box_win32_sysconf(
    int name)
{
    long result = -1;
    if (name == _SC_NPROCESSORS_ONLN) {
        SYSTEM_INFO info;
        GetSystemInfo(&info);
        result = (long)info.dwNumberOfProcessors;
    }

    return result;
}

void box_win32_exe_dir(
    char *out,
    size_t size)
{
    DWORD len = GetModuleFileNameA(NULL, out, (DWORD)size);
    if (!len || len >= size) {
        out[0] = '\0';
        goto done;
    }

    box_win32_forward_slashes(out);
    char *slash = strrchr(out, '/');
    if (slash) {
        *slash = '\0';
    }

done:
    return;
}

void box_win32_temp_dir(
    char *out,
    size_t size)
{
    DWORD len = GetTempPathA((DWORD)size, out);
    if (!len || len >= size) {
        snprintf(out, size, ".");
        goto done;
    }

    box_win32_forward_slashes(out);
    if (out[len - 1] == '/') {
        out[len - 1] = '\0';
    }

done:
    return;
}

#endif
