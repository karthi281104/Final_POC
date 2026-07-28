#ifndef PORTABLE_PTHREAD_H
#define PORTABLE_PTHREAD_H

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

/* This header exists so every other file in the project can just do
 * `#include "portable_pthread.h"` instead of `#include <pthread.h>`,
 * and get working threading either way:
 *   - On Linux/Mac, and on MinGW-w64 (modern Windows GCC), a real
 *     <pthread.h> exists, so we just include it directly - zero
 *     behavior change from before.
 *   - On old "MinGW.org" toolchains (e.g. gcc.exe (MinGW.org GCC-6.3.0)),
 *     <pthread.h> does not exist at all. In that case we fall back to
 *     a small shim built directly on native Win32 threading primitives
 *     (CRITICAL_SECTION, SRWLOCK, CONDITION_VARIABLE, _beginthreadex),
 *     which every Windows GCC distribution has access to via
 *     <windows.h>/<process.h> regardless of how old it is.
 *
 * Every function in the shim is `static` (internal linkage), so this
 * header is safe to include from many different .c files without
 * causing "multiple definition" linker errors. */

#if defined(_WIN32) && defined(__has_include)
    #if __has_include(<pthread.h>)
        #define PORTABLE_PTHREAD_USE_REAL 1
    #else
        #define PORTABLE_PTHREAD_USE_REAL 0
    #endif
#elif defined(_WIN32)
    /* No __has_include support: assume the worst (old toolchain) so we
     * fail safe into the shim rather than a hard compile error. */
    #define PORTABLE_PTHREAD_USE_REAL 0
#else
    #define PORTABLE_PTHREAD_USE_REAL 1
#endif

#if PORTABLE_PTHREAD_USE_REAL

#include <pthread.h>

#else /* ---------------- native Win32 shim ---------------- */

#include <windows.h>
#include <process.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>

/* Manually declare missing APIs if headers are too old */
#ifndef SRWLOCK_INIT
typedef struct { PVOID Ptr; } SRWLOCK_SHIM, *PSRWLOCK_SHIM;
typedef struct { PVOID Ptr; } CONDITION_VARIABLE_SHIM, *PCONDITION_VARIABLE_SHIM;
WINBASEAPI VOID WINAPI InitializeSRWLock(PSRWLOCK_SHIM);
WINBASEAPI VOID WINAPI AcquireSRWLockExclusive(PSRWLOCK_SHIM);
WINBASEAPI VOID WINAPI AcquireSRWLockShared(PSRWLOCK_SHIM);
WINBASEAPI VOID WINAPI ReleaseSRWLockExclusive(PSRWLOCK_SHIM);
WINBASEAPI VOID WINAPI ReleaseSRWLockShared(PSRWLOCK_SHIM);
WINBASEAPI VOID WINAPI InitializeConditionVariable(PCONDITION_VARIABLE_SHIM);
WINBASEAPI BOOL WINAPI SleepConditionVariableCS(PCONDITION_VARIABLE_SHIM, CRITICAL_SECTION*, DWORD);
WINBASEAPI VOID WINAPI WakeConditionVariable(PCONDITION_VARIABLE_SHIM);
WINBASEAPI VOID WINAPI WakeAllConditionVariable(PCONDITION_VARIABLE_SHIM);
#define SRWLOCK SRWLOCK_SHIM
#define CONDITION_VARIABLE CONDITION_VARIABLE_SHIM
#endif

/* ===== struct timespec / clock_gettime ================================
 * Old MinGW.org toolchains sometimes declare struct timespec already
 * (guarded by _TIMESPEC_DEFINED or __struct_timespec_defined) but never
 * provide clock_gettime. */
#if !defined(_TIMESPEC_DEFINED) && !defined(__struct_timespec_defined)
#define _TIMESPEC_DEFINED
#define __struct_timespec_defined
struct timespec {
    time_t tv_sec;
    long   tv_nsec;
};
#endif

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT 138
#endif

static inline int clock_gettime(int clk_id, struct timespec *ts)
{
    if (clk_id == CLOCK_MONOTONIC)
    {
        LARGE_INTEGER freq;
        LARGE_INTEGER counter;
        (void)QueryPerformanceFrequency(&freq);
        (void)QueryPerformanceCounter(&counter);
        ts->tv_sec = (time_t)(counter.QuadPart / freq.QuadPart);
        ts->tv_nsec = (long)(((counter.QuadPart % freq.QuadPart) * 1000000000LL) / freq.QuadPart);
    }
    else
    {
        FILETIME ft;
        ULARGE_INTEGER uli;
        unsigned long long ticks100ns;
        GetSystemTimeAsFileTime(&ft);
        /* Use memcpy to copy FILETIME into ULARGE_INTEGER (same layout) so
         * that static-analysis tools (cppcheck unreadVariable) can see both
         * fields are consumed via the subsequent QuadPart read. */
        (void)memcpy(&uli, &ft, sizeof(uli));
        ticks100ns = (unsigned long long)(uli.QuadPart - 116444736000000000ULL);
        ts->tv_sec  = (time_t)(ticks100ns / 10000000ULL);
        ts->tv_nsec = (long)((ticks100ns % 10000000ULL) * 100ULL);
    }
    return 0;
}

/* ===== mutex ============================================================ */
typedef CRITICAL_SECTION pthread_mutex_t;

static inline int pthread_mutex_init(pthread_mutex_t *m, const void *attr)
{
    (void)attr;
    InitializeCriticalSection(m);
    return 0;
}

static inline int pthread_mutex_lock(pthread_mutex_t *m)
{
    EnterCriticalSection(m);
    return 0;
}

static inline int pthread_mutex_unlock(pthread_mutex_t *m)
{
    LeaveCriticalSection(m);
    return 0;
}

static inline int pthread_mutex_destroy(pthread_mutex_t *m)
{
    DeleteCriticalSection(m);
    return 0;
}

/* ===== reader/writer lock ================================================ */
typedef struct {
    SRWLOCK   srw;
    volatile long writerActive;
} pthread_rwlock_t;

static inline int pthread_rwlock_init(pthread_rwlock_t *rw, const void *attr)
{
    (void)attr;
    InitializeSRWLock(&rw->srw);
    rw->writerActive = 0;
    return 0;
}

static inline int pthread_rwlock_rdlock(pthread_rwlock_t *rw)
{
    AcquireSRWLockShared(&rw->srw);
    return 0;
}

static inline int pthread_rwlock_wrlock(pthread_rwlock_t *rw)
{
    AcquireSRWLockExclusive(&rw->srw);
    rw->writerActive = 1;
    return 0;
}

static inline int pthread_rwlock_unlock(pthread_rwlock_t *rw)
{
    if (rw->writerActive)
    {
        rw->writerActive = 0;
        ReleaseSRWLockExclusive(&rw->srw);
    }
    else
    {
        ReleaseSRWLockShared(&rw->srw);
    }
    return 0;
}

static inline int pthread_rwlock_destroy(pthread_rwlock_t *rw)
{
    (void)rw;
    return 0;
}

/* ===== condition variable ================================================ */
typedef CONDITION_VARIABLE pthread_cond_t;

static inline int pthread_cond_init(pthread_cond_t *cv, const void *attr)
{
    (void)attr;
    InitializeConditionVariable(cv);
    return 0;
}

static inline int pthread_cond_destroy(pthread_cond_t *cv)
{
    (void)cv;
    return 0;
}

static inline int pthread_cond_broadcast(pthread_cond_t *cv)
{
    WakeAllConditionVariable(cv);
    return 0;
}

static inline int pthread_cond_signal(pthread_cond_t *cv)
{
    WakeConditionVariable(cv);
    return 0;
}

static inline int pthread_cond_timedwait(pthread_cond_t *cv, pthread_mutex_t *mutex,
                                   const struct timespec *abstime)
{
    struct timespec now;
    long long deltaMs;
    DWORD waitMs;
    BOOL ok;

    (void)clock_gettime(CLOCK_REALTIME, &now);

    deltaMs = ((long long)(abstime->tv_sec - now.tv_sec) * 1000LL) +
              (((long long)abstime->tv_nsec - (long long)now.tv_nsec) / 1000000LL);
    if (deltaMs < 0)
    {
        deltaMs = 0;
    }
    waitMs = (DWORD)deltaMs;

    ok = SleepConditionVariableCS(cv, mutex, waitMs);
    if (!ok && (GetLastError() == ERROR_TIMEOUT))
    {
        return ETIMEDOUT;
    }
    return 0;
}

/* ===== threads ============================================================ */
typedef HANDLE pthread_t;

typedef struct {
    void *(*startRoutine)(void *);
    void *arg;
} PthreadShimTrampolineArg;

static inline unsigned __stdcall pthreadShimTrampoline(void *p)
{
    PthreadShimTrampolineArg *t = (PthreadShimTrampolineArg *)p;
    void *(*fn)(void *) = t->startRoutine;
    void *a = t->arg;
    free(t);
    (void)fn(a);
    return 0;
}

static inline int pthread_create(pthread_t *thread, const void *attr,
                           void *(*startRoutine)(void *), void *arg)
{
    PthreadShimTrampolineArg *t;
    uintptr_t h;

    (void)attr;
    t = (PthreadShimTrampolineArg *)malloc(sizeof(*t));
    if (t == NULL)
    {
        return -1;
    }
    t->startRoutine = startRoutine;
    t->arg = arg;

    h = _beginthreadex(NULL, 0, pthreadShimTrampoline, t, 0, NULL);
    if (h == 0U)
    {
        free(t);
        return -1;
    }
    *thread = (HANDLE)h;
    return 0;
}

static inline int pthread_join(pthread_t thread, void **retval)
{
    (void)retval;
    (void)WaitForSingleObject(thread, INFINITE);
    (void)CloseHandle(thread);
    return 0;
}

#endif /* PORTABLE_PTHREAD_USE_REAL */

#endif /* PORTABLE_PTHREAD_H */
