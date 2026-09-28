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
#include <intrin.h>
#include <fcntl.h>

/** Fraction of a second the rate of the time stamp counter is measured over */
#define BOX_WIN32_TSC_SPAN (500)

/** Ticks per second of the time stamp counter, zero when it cannot be used */
static double box_win32_tsc_hz = 0;

/** Guards the one time measurement of box_win32_tsc_hz */
static INIT_ONCE box_win32_tsc_once = INIT_ONCE_STATIC_INIT;

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

/** Measure the rate of the time stamp counter, which process cycle times count in.
 *
 * Leaves box_win32_tsc_hz at zero when the counter is not invariant or cannot be
 * read, so box_win32_process_time falls back to GetProcessTimes.
 *
 * @param once Unused
 * @param param Unused
 * @param ctx Unused
 * @return TRUE, the measurement does not fail
 */
static BOOL CALLBACK box_win32_tsc_measure(
    PINIT_ONCE once,
    PVOID param,
    PVOID *ctx)
{
    (void)once;
    (void)param;
    (void)ctx;
#if defined(_M_X64) || defined(_M_IX86)
    int regs[4];
    __cpuid(regs, 0x80000000);
    if ((unsigned)regs[0] < 0x80000007) {
        goto done;
    }

    __cpuid(regs, 0x80000007);
    if (!(regs[3] & (1 << 8))) {
        goto done;
    }

    LARGE_INTEGER freq, start, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    unsigned __int64 tsc_start = __rdtsc();
    do {
        QueryPerformanceCounter(&now);
    } while (now.QuadPart - start.QuadPart < freq.QuadPart / BOX_WIN32_TSC_SPAN);

    unsigned __int64 tsc_end = __rdtsc();
    box_win32_tsc_hz = (double)(tsc_end - tsc_start) * (double)freq.QuadPart /
        (double)(now.QuadPart - start.QuadPart);
done:
#endif

    return TRUE;
}

double box_win32_process_time(void)
{
    double result = 0;
    ULONG64 cycles = 0;
    InitOnceExecuteOnce(&box_win32_tsc_once, box_win32_tsc_measure, NULL, NULL);
    if (box_win32_tsc_hz > 0 && QueryProcessCycleTime(GetCurrentProcess(), &cycles)) {
        result = (double)cycles / box_win32_tsc_hz;
        goto done;
    }

    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    result = (double)usage.ru_utime.tv_sec + (double)usage.ru_utime.tv_usec * 1e-6 +
        (double)usage.ru_stime.tv_sec + (double)usage.ru_stime.tv_usec * 1e-6;
done:
    return result;
}

int box_win32_setenv(
    const char *name,
    const char *value,
    int overwrite)
{
    int result = 0;
    if (!overwrite && getenv(name)) {
        goto done;
    }

    result = _putenv_s(name, value) ? -1 : 0;
done:
    return result;
}

int box_win32_unsetenv(
    const char *name)
{
    return _putenv_s(name, "") ? -1 : 0;
}

FILE* box_win32_fopen_unlinkable(
    const char *path)
{
    FILE *result = NULL;
    HANDLE h = CreateFileA(path, GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        goto done;
    }

    int fd = _open_osfhandle((intptr_t)h, _O_WRONLY | _O_TEXT);
    if (fd == -1) {
        CloseHandle(h);
        goto done;
    }

    result = _fdopen(fd, "w");
    if (!result) {
        _close(fd);
    }

done:
    return result;
}

int box_win32_usleep(
    unsigned int us)
{
    Sleep((DWORD)((us + 999) / 1000));

    return 0;
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
