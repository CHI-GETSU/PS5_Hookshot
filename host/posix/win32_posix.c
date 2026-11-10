/* (linux) The Win32 calls of host/posix/windows.h, on POSIX. Built into the executable and, on its own, into
   hookshot_vk.so (each its own copy, -Bsymbolic: the executable exports nothing of it). */
#define _GNU_SOURCE
#include <windows.h>
#include <mmsystem.h>

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <limits.h>
#ifndef __PROSPERO__
#include <linux/futex.h>
#endif
#include <signal.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <ucontext.h>
#ifdef __PROSPERO__   /* (the console: host/ps5/win32_ps5.h says what changes) */
#include "../ps5/win32_ps5.h"
#endif

/* the guest window (third_party/a64-kit/rt/a64.h A64_HOST_BASE, host/hos.h HOS_GUEST_SPACE): one mapping, all of it
   readable and writable, which VirtualQuery reports as committed without asking the kernel */
#ifdef __PROSPERO__
#define GUEST_LO HOS_PS5_GUEST_LO
#define GUEST_HI HOS_PS5_GUEST_HI
#else
#define GUEST_LO 0x100000000000ULL
#define GUEST_HI (GUEST_LO + (1ULL << 39))
#endif

/* ---- errors ---- */
static __thread DWORD last_error;
DWORD GetLastError(void) { return last_error; }
void SetLastError(DWORD e) { last_error = e; }
static DWORD error_of(int e) {
    switch (e) {
    case 0: return ERROR_SUCCESS;
    case ENOENT: return ERROR_FILE_NOT_FOUND;
    case ENOTDIR: return ERROR_PATH_NOT_FOUND;
    case EEXIST: return ERROR_ALREADY_EXISTS;
    case EACCES: case EPERM: case EISDIR: case EROFS: return ERROR_ACCESS_DENIED;
    case ENOMEM: return ERROR_NOT_ENOUGH_MEMORY;
    case EBADF: return ERROR_INVALID_HANDLE;
    case ETIMEDOUT: return ERROR_TIMEOUT;
    default: return ERROR_INVALID_PARAMETER;
    }
}
static BOOL fail_errno(void) { last_error = error_of(errno); return FALSE; }

/* ---- handles ---- */
enum { H_THREAD = 0x48540001, H_EVENT, H_SEMA, H_TIMER, H_FILE, H_FIND };
typedef struct { int type; } HObj;
static int is(HANDLE h, int type) { return h && h != INVALID_HANDLE_VALUE && (uintptr_t)h > 0x10000 && ((HObj *)h)->type == type; }

static int64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}
static struct timespec abs_after_ms(DWORD ms) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) ts.tv_sec++, ts.tv_nsec -= 1000000000L;
    return ts;
}
static void monitor_init(pthread_mutex_t *mu, pthread_cond_t *cv) {
    pthread_condattr_t a;
    pthread_condattr_init(&a);
    pthread_condattr_setclock(&a, CLOCK_MONOTONIC);
    pthread_cond_init(cv, &a);
    pthread_condattr_destroy(&a);
    pthread_mutex_init(mu, NULL);
}
/* waits for pred() under mu, at most ms: WAIT_OBJECT_0 or WAIT_TIMEOUT */
#define MONITOR_WAIT(mu, cv, pred, ms, result)                                                                        \
    do {                                                                                                              \
        struct timespec until_ = abs_after_ms(ms);                                                                    \
        (result) = WAIT_OBJECT_0;                                                                                     \
        while (!(pred)) {                                                                                             \
            if ((ms) == INFINITE) pthread_cond_wait((cv), (mu));                                                      \
            else if (pthread_cond_timedwait((cv), (mu), &until_) == ETIMEDOUT && !(pred)) { (result) = WAIT_TIMEOUT; break; } \
        }                                                                                                             \
    } while (0)

/* ---- futexes: condition variables, WaitOnAddress ---- */
static long futex(volatile uint32_t *a, int op, uint32_t v, const struct timespec *ts) {
#ifdef __PROSPERO__
    return hos_ps5_futex(a, op, v, ts);
#else
    return syscall(SYS_futex, a, op, v, ts, NULL, 0);
#endif
}
static struct timespec rel_ms(DWORD ms) {
    struct timespec ts = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000L};
    return ts;
}
void WakeConditionVariable(CONDITION_VARIABLE *cv) {
    __atomic_add_fetch(&cv->seq, 1, __ATOMIC_SEQ_CST);
    futex(&cv->seq, FUTEX_WAKE_PRIVATE, 1, NULL);
}
void WakeAllConditionVariable(CONDITION_VARIABLE *cv) {
    __atomic_add_fetch(&cv->seq, 1, __ATOMIC_SEQ_CST);
    futex(&cv->seq, FUTEX_WAKE_PRIVATE, INT_MAX, NULL);
}
static BOOL cv_wait(CONDITION_VARIABLE *cv, DWORD ms, void (*unlock)(void *), void (*relock)(void *, ULONG), void *l, ULONG flags) {
    const uint32_t seq = __atomic_load_n(&cv->seq, __ATOMIC_SEQ_CST);
    unlock(l);
    struct timespec ts = rel_ms(ms);
    const long r = futex(&cv->seq, FUTEX_WAIT_PRIVATE, seq, ms == INFINITE ? NULL : &ts);
    const int timed_out = r == -1 && errno == ETIMEDOUT;
    relock(l, flags);
    if (timed_out) { last_error = ERROR_TIMEOUT; return FALSE; }
    return TRUE;
}
#ifdef __PROSPERO__
static void srw_unlock(void *l) { hos_srw_release((SRWLOCK *)l); }
static void srw_relock(void *l, ULONG flags) {
    if (flags & CONDITION_VARIABLE_LOCKMODE_SHARED) hos_srw_lock_shared((SRWLOCK *)l);
    else hos_srw_lock((SRWLOCK *)l);
}
#else
static void srw_unlock(void *l) { pthread_rwlock_unlock((pthread_rwlock_t *)l); }
static void srw_relock(void *l, ULONG flags) {
    if (flags & CONDITION_VARIABLE_LOCKMODE_SHARED) pthread_rwlock_rdlock((pthread_rwlock_t *)l);
    else pthread_rwlock_wrlock((pthread_rwlock_t *)l);
}
#endif
static void cs_unlock(void *l) { pthread_mutex_unlock((pthread_mutex_t *)l); }
static void cs_relock(void *l, ULONG flags) { (void)flags; pthread_mutex_lock((pthread_mutex_t *)l); }
BOOL SleepConditionVariableSRW(CONDITION_VARIABLE *cv, SRWLOCK *l, DWORD ms, ULONG flags) {
    return cv_wait(cv, ms, srw_unlock, srw_relock, l, flags);
}
BOOL SleepConditionVariableCS(CONDITION_VARIABLE *cv, CRITICAL_SECTION *cs, DWORD ms) {
    return cv_wait(cv, ms, cs_unlock, cs_relock, cs, 0);
}
void InitializeCriticalSection(CRITICAL_SECTION *cs) {
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(cs, &a);
    pthread_mutexattr_destroy(&a);
}
/* (any size: the futex is the aligned 32-bit word holding the address, woken by any change near it) */
BOOL WaitOnAddress(volatile void *addr, void *compare, SIZE_T size, DWORD ms) {
    volatile uint32_t *w = (volatile uint32_t *)((uintptr_t)addr & ~(uintptr_t)3);
    const uint32_t seen = *w;
    if (memcmp((const void *)addr, compare, size)) return TRUE;
    struct timespec ts = rel_ms(ms);
    const long r = futex(w, FUTEX_WAIT_PRIVATE, seen, ms == INFINITE ? NULL : &ts);
    if (r == -1 && errno == ETIMEDOUT) { last_error = ERROR_TIMEOUT; return FALSE; }
    return TRUE;
}
void WakeByAddressSingle(void *addr) { futex((volatile uint32_t *)((uintptr_t)addr & ~(uintptr_t)3), FUTEX_WAKE_PRIVATE, INT_MAX, NULL); }
void WakeByAddressAll(void *addr) { futex((volatile uint32_t *)((uintptr_t)addr & ~(uintptr_t)3), FUTEX_WAKE_PRIVATE, INT_MAX, NULL); }

/* ---- time ---- */
void Sleep(DWORD ms) {
    if (!ms) { sched_yield(); return; }
    struct timespec ts = rel_ms(ms);
    while (nanosleep(&ts, &ts) && errno == EINTR) {}
}
void GetSystemTimeAsFileTime(FILETIME *ft) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    const uint64_t t = ((uint64_t)ts.tv_sec + 11644473600ULL) * 10000000ULL + (uint64_t)ts.tv_nsec / 100;
    ft->dwLowDateTime = (DWORD)t, ft->dwHighDateTime = (DWORD)(t >> 32);
}
typedef struct { HObj h; int64_t due; } HTimer;
HANDLE CreateWaitableTimerExW(void *attr, LPCWSTR name, DWORD flags, DWORD access) {
    (void)attr, (void)name, (void)flags, (void)access;
    HTimer *t = (HTimer *)calloc(1, sizeof *t);
    t->h.type = H_TIMER;
    return t;
}
BOOL SetWaitableTimerEx(HANDLE h, const LARGE_INTEGER *due, LONG period, void *fn, void *arg, void *ctx, ULONG delay) {
    (void)period, (void)fn, (void)arg, (void)ctx, (void)delay;
    if (!is(h, H_TIMER)) { last_error = ERROR_INVALID_HANDLE; return FALSE; }
    /* (relative times only: negative, in 100 ns units) */
    ((HTimer *)h)->due = now_ns() + (due->QuadPart < 0 ? -due->QuadPart * 100 : 0);
    return TRUE;
}

/* ---- threads ---- */
typedef struct {
    HObj h;
    pthread_t th;
    volatile pid_t tid;
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int done, priority;
    DWORD code;
    volatile int refs;
    LPTHREAD_START_ROUTINE fn;
    void *arg;
    char name[64];
} HThread;
#ifdef __PROSPERO__
#define HOS_GETTID() hos_ps5_tid()
#else
#define HOS_GETTID() (pid_t)syscall(SYS_gettid)
#endif
static __thread HThread *self_thread;
static void thread_unref(HThread *t) {
    if (__atomic_sub_fetch(&t->refs, 1, __ATOMIC_ACQ_REL) == 0) {
        pthread_mutex_destroy(&t->mu);
        pthread_cond_destroy(&t->cv);
        free(t);
    }
}
static HThread *thread_new(void) {
    HThread *t = (HThread *)calloc(1, sizeof *t);
    t->h.type = H_THREAD;
    monitor_init(&t->mu, &t->cv);
    return t;
}
static HThread *thread_self(void) {
    if (!self_thread) {   /* (a thread this file did not start: the main thread, a library's) */
        HThread *t = thread_new();
        t->th = pthread_self();
        t->tid = HOS_GETTID();
        t->refs = 1;   /* (kept for the thread's life) */
        self_thread = t;
    }
    return self_thread;
}
#define PSEUDO_THREAD ((HANDLE)(intptr_t)-2)
static HThread *thread_of(HANDLE h) {
    if (h == PSEUDO_THREAD) return thread_self();
    return is(h, H_THREAD) ? (HThread *)h : NULL;
}
static void thread_finish(DWORD code) {
    HThread *t = self_thread;
    if (!t) return;
    pthread_mutex_lock(&t->mu);
    t->done = 1, t->code = code;
    pthread_cond_broadcast(&t->cv);
    pthread_mutex_unlock(&t->mu);
    self_thread = NULL;
    thread_unref(t);
}
static void *thread_entry(void *p) {
    HThread *t = (HThread *)p;
    self_thread = t;
    t->tid = HOS_GETTID();
    thread_finish(t->fn(t->arg));
    return NULL;
}
HANDLE CreateThread(void *attr, SIZE_T stack, LPTHREAD_START_ROUTINE fn, void *arg, DWORD flags, DWORD *id) {
    (void)attr;
    HThread *t = thread_new();
    t->fn = fn, t->arg = arg, t->refs = 2;   /* (the handle's and the thread's own) */
    if (flags & CREATE_SUSPENDED) fprintf(stderr, "[win32] CreateThread: CREATE_SUSPENDED is not supported: started at once\n");
#ifdef __PROSPERO__   /* (the stack in direct memory, freed once the thread has ended: host/ps5/win32_ps5.c) */
    int hos_ps5_thread_start(pthread_t *th, size_t stack, void *(*fn)(void *), void *arg);
    const int r = hos_ps5_thread_start(&t->th, stack ? stack : (8u << 20), thread_entry, t);
#else
    pthread_attr_t a;
    pthread_attr_init(&a);
    /* (Windows' default is the executable's: 8 MB here, as hookshot.exe's /stack; guest threads ask 64 MB) */
    pthread_attr_setstacksize(&a, stack ? (stack + 0xFFFF) & ~(SIZE_T)0xFFFF : (8u << 20));
    pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    const int r = pthread_create(&t->th, &a, thread_entry, t);
    pthread_attr_destroy(&a);
#endif
    if (r) {
        free(t);
        last_error = error_of(r);
        return NULL;
    }
    if (id) {
        while (!t->tid) sched_yield();
        *id = (DWORD)t->tid;
    }
    return t;
}
HANDLE GetCurrentThread(void) { return PSEUDO_THREAD; }
DWORD GetCurrentThreadId(void) { return (DWORD)thread_self()->tid; }
DWORD GetThreadId(HANDLE h) {
    HThread *t = thread_of(h);
    if (!t) return 0;
    while (!t->tid && !t->done) sched_yield();
    return (DWORD)t->tid;
}
DWORD GetProcessId(HANDLE p) { (void)p; return (DWORD)getpid(); }
HANDLE OpenThread(DWORD access, BOOL inherit, DWORD id) {
    (void)access, (void)inherit, (void)id;
    last_error = ERROR_NOT_SUPPORTED;
    return NULL;
}
BOOL DuplicateHandle(HANDLE sp, HANDLE src, HANDLE tp, HANDLE *out, DWORD access, BOOL inherit, DWORD options) {
    (void)sp, (void)tp, (void)access, (void)inherit, (void)options;
    HThread *t = thread_of(src);
    if (!t) { last_error = ERROR_NOT_SUPPORTED; return FALSE; }
    __atomic_add_fetch(&t->refs, 1, __ATOMIC_ACQ_REL);
    *out = t;
    return TRUE;
}
/* (thread priorities: only lowered, with nice values: an unprivileged process cannot raise them, nor take back a lowering) */
BOOL SetThreadPriority(HANDLE h, int p) {
    HThread *t = thread_of(h);
    if (!t) { last_error = ERROR_INVALID_HANDLE; return FALSE; }
#ifdef __PROSPERO__   /* (the renderer's own threads, made without CreateThread, into the sampling profiler: HOS_PS5_SAMPLE) */
    if (h == PSEUDO_THREAD) {
        void hos_ps5_sample_register(void);
        hos_ps5_sample_register();
    }
#endif
    t->priority = p;
    const int nice = p <= THREAD_PRIORITY_IDLE ? 19 : p <= THREAD_PRIORITY_LOWEST ? 10 : p == THREAD_PRIORITY_BELOW_NORMAL ? 5 : 0;
    if (nice && t->tid) setpriority(PRIO_PROCESS, (id_t)t->tid, nice);
    return TRUE;
}
int GetThreadPriority(HANDLE h) {
    HThread *t = thread_of(h);
    return t ? t->priority : 0;
}
DWORD_PTR SetThreadAffinityMask(HANDLE h, DWORD_PTR mask) {
    HThread *t = thread_of(h);
    if (!t || !mask) { last_error = ERROR_INVALID_PARAMETER; return 0; }
#ifdef __PROSPERO__   /* (the console's own calls: pthread_*affinity_np answers nothing there) */
    int32_t scePthreadGetaffinity(pthread_t thread, uint64_t *mask);
    int32_t scePthreadSetaffinity(pthread_t thread, uint64_t mask);
    uint64_t was = 0;
    scePthreadGetaffinity(t->th, &was);
    if (scePthreadSetaffinity(t->th, (uint64_t)mask) != 0) { last_error = ERROR_INVALID_PARAMETER; return 0; }
    return was ? (DWORD_PTR)was : 1;
#else
    cpu_set_t old, want;
    CPU_ZERO(&old);
    pthread_getaffinity_np(t->th, sizeof old, &old);
    CPU_ZERO(&want);
    for (int i = 0; i < 64; i++) if (mask >> i & 1) CPU_SET(i, &want);
    if (pthread_setaffinity_np(t->th, sizeof want, &want)) { last_error = ERROR_INVALID_PARAMETER; return 0; }
    DWORD_PTR r = 0;
    for (int i = 0; i < 64; i++) if (CPU_ISSET(i, &old)) r |= (DWORD_PTR)1 << i;
    return r ? r : 1;
#endif
}
BOOL GetProcessAffinityMask(HANDLE p, DWORD_PTR *proc, DWORD_PTR *sys) {
    (void)p;
    DWORD_PTR m = 0;
#ifdef __PROSPERO__   /* (no sched_getaffinity: the calling thread's mask, which threads inherit) */
    int32_t scePthreadGetaffinity(pthread_t thread, uint64_t *mask);
    uint64_t own = 0;
    scePthreadGetaffinity(pthread_self(), &own);
    m = (DWORD_PTR)own;
#else
    cpu_set_t s;
    CPU_ZERO(&s);
    sched_getaffinity(0, sizeof s, &s);
    for (int i = 0; i < 64; i++) if (CPU_ISSET(i, &s)) m |= (DWORD_PTR)1 << i;
#endif
    long n = sysconf(_SC_NPROCESSORS_CONF);
    *proc = m;
    *sys = n >= 64 ? ~(DWORD_PTR)0 : (((DWORD_PTR)1 << n) - 1);
    return TRUE;
}
int WideCharToMultiByte(UINT cp, DWORD flags, LPCWSTR w, int n, LPSTR s, int cap, LPCSTR def, BOOL *used) {
    (void)cp, (void)flags, (void)def;
    if (used) *used = FALSE;
    int out = 0;
    for (int i = 0; n < 0 ? 1 : i < n; i++) {
        uint32_t c = (uint32_t)w[i];
        char b[4];
        int k = c < 0x80 ? (b[0] = (char)c, 1)
              : c < 0x800 ? (b[0] = (char)(0xC0 | c >> 6), b[1] = (char)(0x80 | (c & 0x3F)), 2)
              : c < 0x10000 ? (b[0] = (char)(0xE0 | c >> 12), b[1] = (char)(0x80 | (c >> 6 & 0x3F)), b[2] = (char)(0x80 | (c & 0x3F)), 3)
              : (b[0] = (char)(0xF0 | c >> 18), b[1] = (char)(0x80 | (c >> 12 & 0x3F)), b[2] = (char)(0x80 | (c >> 6 & 0x3F)),
                 b[3] = (char)(0x80 | (c & 0x3F)), 4);
        if (cap) {
            if (out + k > cap) { last_error = ERROR_INVALID_PARAMETER; return 0; }
            memcpy(s + out, b, (size_t)k);
        }
        out += k;
        if (n < 0 && !c) break;
    }
    return out;
}
int MultiByteToWideChar(UINT cp, DWORD flags, LPCSTR s, int n, LPWSTR w, int cap) {
    (void)cp, (void)flags;
    const unsigned char *p = (const unsigned char *)s;
    const unsigned char *end = n < 0 ? NULL : p + n;
    int out = 0;
    for (;;) {
        if (end && p >= end) break;
        uint32_t c = *p++;
        if (c >= 0xF0 && (!end || p + 2 < end)) c = (c & 7) << 18 | (p[0] & 0x3Fu) << 12 | (p[1] & 0x3Fu) << 6 | (p[2] & 0x3Fu), p += 3;
        else if (c >= 0xE0 && (!end || p + 1 < end)) c = (c & 15) << 12 | (p[0] & 0x3Fu) << 6 | (p[1] & 0x3Fu), p += 2;
        else if (c >= 0xC0 && (!end || p < end)) c = (c & 31) << 6 | (p[0] & 0x3Fu), p += 1;
        if (cap) {
            if (out >= cap) { last_error = ERROR_INVALID_PARAMETER; return 0; }
            w[out] = (wchar_t)c;
        }
        out++;
        if (!end && !c) break;
    }
    return out;
}
HRESULT SetThreadDescription(HANDLE h, PCWSTR name) {
    HThread *t = thread_of(h);
    if (!t) return (HRESULT)0x80070006;
    WideCharToMultiByte(CP_UTF8, 0, name, -1, t->name, sizeof t->name - 1, NULL, NULL);
    t->name[sizeof t->name - 1] = 0;
    char shortname[16];
    snprintf(shortname, sizeof shortname, "%s", t->name);   /* (the kernel keeps 15 characters) */
    pthread_setname_np(t->th, shortname);
    return S_OK;
}
HRESULT GetThreadDescription(HANDLE h, PWSTR *name) {
    HThread *t = thread_of(h);
    if (!t) return (HRESULT)0x80070006;
    const int n = MultiByteToWideChar(CP_UTF8, 0, t->name, -1, NULL, 0);
    *name = (PWSTR)calloc((size_t)n + 1, sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, t->name, -1, *name, n);
    return S_OK;
}
void *LocalFree(void *p) { free(p); return NULL; }
static void filetime_ns(FILETIME *ft, int64_t ns) {
    const uint64_t t = (uint64_t)ns / 100;
    ft->dwLowDateTime = (DWORD)t, ft->dwHighDateTime = (DWORD)(t >> 32);
}
BOOL GetThreadTimes(HANDLE h, FILETIME *create, FILETIME *exit_, FILETIME *kernel, FILETIME *user) {
    HThread *t = thread_of(h);
    clockid_t clk;
    struct timespec ts;
    if (!t || t->done || pthread_getcpuclockid(t->th, &clk) || clock_gettime(clk, &ts)) { last_error = ERROR_INVALID_HANDLE; return FALSE; }
    filetime_ns(create, 0), filetime_ns(exit_, 0), filetime_ns(kernel, 0);
    filetime_ns(user, (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec);
    return TRUE;
}
/* (no suspending another thread: the samplers that do it find nothing to sample) */
DWORD SuspendThread(HANDLE h) { (void)h; last_error = ERROR_NOT_SUPPORTED; return (DWORD)-1; }
DWORD ResumeThread(HANDLE h) { (void)h; last_error = ERROR_NOT_SUPPORTED; return (DWORD)-1; }
BOOL GetThreadContext(HANDLE h, CONTEXT *c) { (void)h, (void)c; last_error = ERROR_NOT_SUPPORTED; return FALSE; }
BOOL SetThreadContext(HANDLE h, const CONTEXT *c) { (void)h, (void)c; last_error = ERROR_NOT_SUPPORTED; return FALSE; }
BOOL GetExitCodeThread(HANDLE h, DWORD *code) {
    HThread *t = thread_of(h);
    if (!t) return FALSE;
    pthread_mutex_lock(&t->mu);
    *code = t->done ? t->code : STILL_ACTIVE;
    pthread_mutex_unlock(&t->mu);
    return TRUE;
}
void ExitThread(DWORD code) {
    thread_finish(code);
    pthread_exit(NULL);
}
void ExitProcess(UINT code) {
    /* (at once, as Windows ends the other threads first: exit() would run static destructors under running threads) */
    fflush(NULL);
#ifdef __PROSPERO__
    hos_ps5_exit((int)code);
#else
    _exit((int)code);
#endif
}
BOOL TerminateProcess(HANDLE p, UINT code) { (void)p; ExitProcess(code); }

/* ---- events and semaphores ---- */
typedef struct {
    HObj h;
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int manual, state;     /* (events) */
    LONG count, max;       /* (semaphores) */
} HEvent;
HANDLE CreateEventA(void *attr, BOOL manual, BOOL initial, LPCSTR name) {
    (void)attr, (void)name;
    HEvent *e = (HEvent *)calloc(1, sizeof *e);
    e->h.type = H_EVENT;
    e->manual = manual, e->state = initial;
    monitor_init(&e->mu, &e->cv);
    return e;
}
BOOL SetEvent(HANDLE h) {
    if (!is(h, H_EVENT)) { last_error = ERROR_INVALID_HANDLE; return FALSE; }
    HEvent *e = (HEvent *)h;
    pthread_mutex_lock(&e->mu);
    e->state = 1;
    if (e->manual) pthread_cond_broadcast(&e->cv);
    else pthread_cond_signal(&e->cv);
    pthread_mutex_unlock(&e->mu);
    return TRUE;
}
BOOL ResetEvent(HANDLE h) {
    if (!is(h, H_EVENT)) { last_error = ERROR_INVALID_HANDLE; return FALSE; }
    HEvent *e = (HEvent *)h;
    pthread_mutex_lock(&e->mu);
    e->state = 0;
    pthread_mutex_unlock(&e->mu);
    return TRUE;
}
HANDLE CreateSemaphoreA(void *attr, LONG initial, LONG max, LPCSTR name) {
    (void)attr, (void)name;
    HEvent *e = (HEvent *)calloc(1, sizeof *e);
    e->h.type = H_SEMA;
    e->count = initial, e->max = max;
    monitor_init(&e->mu, &e->cv);
    return e;
}
BOOL ReleaseSemaphore(HANDLE h, LONG n, LONG *prev) {
    if (!is(h, H_SEMA)) { last_error = ERROR_INVALID_HANDLE; return FALSE; }
    HEvent *e = (HEvent *)h;
    pthread_mutex_lock(&e->mu);
    if (prev) *prev = e->count;
    if (e->count + n > e->max) { pthread_mutex_unlock(&e->mu); last_error = ERROR_INVALID_PARAMETER; return FALSE; }
    e->count += n;
    pthread_cond_broadcast(&e->cv);
    pthread_mutex_unlock(&e->mu);
    return TRUE;
}
DWORD WaitForSingleObject(HANDLE h, DWORD ms) {
    DWORD r = WAIT_FAILED;
    if (is(h, H_EVENT)) {
        HEvent *e = (HEvent *)h;
        pthread_mutex_lock(&e->mu);
        MONITOR_WAIT(&e->mu, &e->cv, e->state, ms, r);
        if (r == WAIT_OBJECT_0 && !e->manual) e->state = 0;
        pthread_mutex_unlock(&e->mu);
    } else if (is(h, H_SEMA)) {
        HEvent *e = (HEvent *)h;
        pthread_mutex_lock(&e->mu);
        MONITOR_WAIT(&e->mu, &e->cv, e->count > 0, ms, r);
        if (r == WAIT_OBJECT_0) e->count--;
        pthread_mutex_unlock(&e->mu);
    } else if (thread_of(h)) {
        HThread *t = thread_of(h);
        pthread_mutex_lock(&t->mu);
        MONITOR_WAIT(&t->mu, &t->cv, t->done, ms, r);
        pthread_mutex_unlock(&t->mu);
    } else if (is(h, H_TIMER)) {
        const int64_t due = ((HTimer *)h)->due;
        struct timespec ts = {(time_t)(due / 1000000000LL), (long)(due % 1000000000LL)};
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR) {}
        r = WAIT_OBJECT_0;
    } else {
        last_error = ERROR_INVALID_HANDLE;
    }
    return r;
}
DWORD WaitForMultipleObjects(DWORD n, const HANDLE *hs, BOOL all, DWORD ms) {
    const int64_t until = ms == INFINITE ? INT64_MAX : now_ns() + (int64_t)ms * 1000000LL;
    for (;;) {   /* (polled: nothing waits on several objects in a hot path) */
        DWORD ready = 0, first = WAIT_TIMEOUT;
        for (DWORD i = 0; i < n; i++)
            if (WaitForSingleObject(hs[i], 0) == WAIT_OBJECT_0) {
                ready++;
                if (first == WAIT_TIMEOUT) first = WAIT_OBJECT_0 + i;
                if (!all) return first;
            }
        if (all && ready == n) return WAIT_OBJECT_0;
        if (now_ns() >= until) return WAIT_TIMEOUT;
        Sleep(1);
    }
}

/* ---- fibers ---- */
#ifdef __PROSPERO__
/* (PS5) A stack switch of our own instead of swapcontext: FreeBSD's contexts carry the thread's fs base (_MC_HASBASES), so a
   fiber made on one thread and resumed on another (nn::os fibers move between the nn.npln threads) gave that thread the
   first one's thread block: two threads on one emulated-TLS array, and the heap corrupted under them (boot4). This saves
   what a C call must keep, as glibc's swapcontext effectively does on Linux: rbx, rbp, r12-r15, MXCSR and the x87 control
   word; the stack pointer goes to *save and the other one is loaded. A new fiber's stack is laid out as a switch leaves
   one, returning into hos_fiber_entry, which calls r12(r13) on a 16-byte aligned stack. */
void hos_fiber_swap(void **save_sp, void *load_sp);
void hos_fiber_entry(void);
__asm__(".text\n"
        ".globl hos_fiber_swap\n"
        ".type hos_fiber_swap, @function\n"
        ".p2align 4\n"
        "hos_fiber_swap:\n"
        "    pushq %rbp\n"
        "    pushq %rbx\n"
        "    pushq %r12\n"
        "    pushq %r13\n"
        "    pushq %r14\n"
        "    pushq %r15\n"
        "    subq $16, %rsp\n"
        "    stmxcsr 8(%rsp)\n"
        "    fnstcw (%rsp)\n"
        "    movq %rsp, (%rdi)\n"
        "    movq %rsi, %rsp\n"
        "    fldcw (%rsp)\n"
        "    ldmxcsr 8(%rsp)\n"
        "    addq $16, %rsp\n"
        "    popq %r15\n"
        "    popq %r14\n"
        "    popq %r13\n"
        "    popq %r12\n"
        "    popq %rbx\n"
        "    popq %rbp\n"
        "    ret\n"
        ".size hos_fiber_swap, .-hos_fiber_swap\n"
        ".globl hos_fiber_entry\n"
        ".type hos_fiber_entry, @function\n"
        ".p2align 4\n"
        "hos_fiber_entry:\n"
        "    movq %r13, %rdi\n"
        "    andq $-16, %rsp\n"
        "    callq *%r12\n"
        "    ud2\n"
        ".size hos_fiber_entry, .-hos_fiber_entry\n");
#endif
typedef struct {
    ucontext_t uc;
#ifdef __PROSPERO__
    void *sp;   /* (where hos_fiber_swap left the fiber's stack) */
#endif
    void *param, *stack;
    size_t stack_size;
    LPFIBER_START_ROUTINE fn;
    int is_thread;
} Fiber;
static __thread Fiber *cur_fiber;
void *ConvertThreadToFiberEx(void *param, DWORD flags) {
    (void)flags;
    Fiber *f = (Fiber *)calloc(1, sizeof *f);
    f->param = param, f->is_thread = 1;
    cur_fiber = f;
    return f;
}
void *ConvertThreadToFiber(void *param) { return ConvertThreadToFiberEx(param, 0); }
BOOL ConvertFiberToThread(void) {
    if (!cur_fiber || !cur_fiber->is_thread) return FALSE;
    free(cur_fiber);
    cur_fiber = NULL;
    return TRUE;
}
#ifdef __PROSPERO__
void hos_fiber_start(Fiber *f) {
    f->fn(f->param);
    ExitThread(0);   /* (a fiber function that returns ends its thread, as on Windows) */
}
#endif
static void fiber_start(unsigned lo, unsigned hi) {
    Fiber *f = (Fiber *)(uintptr_t)((uint64_t)hi << 32 | lo);
    f->fn(f->param);
    ExitThread(0);   /* (a fiber function that returns ends its thread, as on Windows) */
}
void *CreateFiberEx(SIZE_T commit, SIZE_T reserve, DWORD flags, LPFIBER_START_ROUTINE fn, void *param) {
    (void)flags;
    size_t size = reserve > commit ? reserve : commit;
    if (size < (1u << 20)) size = 1u << 20;
    size = (size + 0xFFFF) & ~(size_t)0xFFFF;
#ifdef __PROSPERO__   /* (direct memory, with an unmapped guard page below: host/ps5/win32_ps5.c) */
    void *hos_ps5_stack_alloc(size_t *size);
    void *stack = hos_ps5_stack_alloc(&size);
    if (!stack) { last_error = ERROR_NOT_ENOUGH_MEMORY; return NULL; }
#else
    void *stack = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_STACK, -1, 0);
    if (stack == MAP_FAILED) { last_error = ERROR_NOT_ENOUGH_MEMORY; return NULL; }
    mprotect(stack, 4096, PROT_NONE);   /* (a guard page at the bottom) */
#endif
    Fiber *f = (Fiber *)calloc(1, sizeof *f);
    f->param = param, f->fn = fn, f->stack = stack, f->stack_size = size;
#ifdef __PROSPERO__
    {
        void hos_fiber_start(Fiber *f);
        uint64_t *sp = (uint64_t *)(((uintptr_t)stack + size) & ~(uintptr_t)15) - 10;
        uint16_t fcw;
        __asm__ volatile("fnstcw %0" : "=m"(fcw));
        sp[0] = fcw, sp[1] = __builtin_ia32_stmxcsr();   /* (the creating thread's FP state, as a new thread gets) */
        sp[2] = sp[3] = 0;                                /* r15, r14 */
        sp[4] = (uint64_t)(uintptr_t)f;                   /* r13: hos_fiber_start's argument */
        sp[5] = (uint64_t)(uintptr_t)hos_fiber_start;     /* r12 */
        sp[6] = sp[7] = 0;                                /* rbx, rbp */
        sp[8] = (uint64_t)(uintptr_t)hos_fiber_entry;     /* the return address */
        sp[9] = 0;
        f->sp = sp;
        return f;
    }
#endif
    getcontext(&f->uc);
    f->uc.uc_stack.ss_sp = stack;
    f->uc.uc_stack.ss_size = size;
    f->uc.uc_link = NULL;
    const uint64_t p = (uint64_t)(uintptr_t)f;
    makecontext(&f->uc, (void (*)(void))fiber_start, 2, (unsigned)p, (unsigned)(p >> 32));
    return f;
}
void *CreateFiber(SIZE_T stack, LPFIBER_START_ROUTINE fn, void *param) { return CreateFiberEx(stack, stack, 0, fn, param); }
void SwitchToFiber(void *to) {
    Fiber *from = cur_fiber;
    if (!from || from == to) return;
    cur_fiber = (Fiber *)to;
#ifdef __PROSPERO__
    hos_fiber_swap(&from->sp, ((Fiber *)to)->sp);
#else
    swapcontext(&from->uc, &((Fiber *)to)->uc);
#endif
}
void DeleteFiber(void *p) {
    Fiber *f = (Fiber *)p;
    if (!f) return;
    if (f == cur_fiber) ExitThread(0);   /* (deleting the running fiber ends the thread) */
#ifdef __PROSPERO__
    void hos_ps5_stack_free(void *stack, size_t size);
    if (f->stack) hos_ps5_stack_free(f->stack, f->stack_size);
#else
    if (f->stack) munmap(f->stack, f->stack_size);
#endif
    free(f);
}
void *GetCurrentFiber(void) { return cur_fiber; }
void *GetFiberData(void) { return cur_fiber ? cur_fiber->param : NULL; }

/* ---- processors ---- */
static int read_int(const char *path) {
    FILE *f = fopen(path, "r");
    int v = -1;
    if (f) {
        if (fscanf(f, "%d", &v) != 1) v = -1;
        fclose(f);
    }
    return v;
}
BOOL GetLogicalProcessorInformation(SYSTEM_LOGICAL_PROCESSOR_INFORMATION *info, DWORD *len) {
    /* one RelationProcessorCore entry per physical core, its logical processors (SMT siblings) in the mask */
    int core_of[64], pkg_of[64], n = 0;
    long cpus = sysconf(_SC_NPROCESSORS_CONF);
    if (cpus > 64) cpus = 64;
    ULONG_PTR masks[64];
    int keys[64][2];
    for (int i = 0; i < cpus; i++) {
        char p[128];
        snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/topology/core_id", i);
        core_of[i] = read_int(p);
        snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", i);
        pkg_of[i] = read_int(p);
#ifdef __PROSPERO__   /* (no /sys: 8 Zen 2 cores, logical processors 2k and 2k+1 share core k) */
        core_of[i] = i / 2, pkg_of[i] = 0;
#endif
        if (core_of[i] < 0) core_of[i] = i, pkg_of[i] = 0;
        int k = 0;
        while (k < n && (keys[k][0] != pkg_of[i] || keys[k][1] != core_of[i])) k++;
        if (k == n) keys[n][0] = pkg_of[i], keys[n][1] = core_of[i], masks[n] = 0, n++;
        masks[k] |= (ULONG_PTR)1 << i;
    }
    const DWORD need = (DWORD)(n * sizeof *info);
    if (*len < need) { *len = need; last_error = 122; return FALSE; }
    memset(info, 0, need);
    for (int k = 0; k < n; k++) {
        info[k].ProcessorMask = masks[k];
        info[k].Relationship = RelationProcessorCore;
        info[k].ProcessorCore.Flags = __builtin_popcountll(masks[k]) > 1;
    }
    *len = need;
    return TRUE;
}
void GetSystemInfo(SYSTEM_INFO *si) {
    memset(si, 0, sizeof *si);
    si->wProcessorArchitecture = 9;   /* (PROCESSOR_ARCHITECTURE_AMD64) */
    si->dwPageSize = (DWORD)sysconf(_SC_PAGESIZE);
    si->dwAllocationGranularity = 0x10000;
    si->dwNumberOfProcessors = (DWORD)sysconf(_SC_NPROCESSORS_ONLN);
    DWORD_PTR proc, sys;
    GetProcessAffinityMask(NULL, &proc, &sys);
    si->dwActiveProcessorMask = proc;
    si->lpMinimumApplicationAddress = (void *)0x10000;
    si->lpMaximumApplicationAddress = (void *)0x7FFFFFFEFFFFULL;
}
void OutputDebugStringA(LPCSTR s) { fputs(s, stderr); }

/* ---- memory ---- */
#ifndef __PROSPERO__   /* (the console's: host/ps5/win32_ps5.c) */
static int prot_of(DWORD p) {
    switch (p & 0xFF) {
    case PAGE_NOACCESS: return PROT_NONE;
    case PAGE_READONLY: return PROT_READ;
    case PAGE_EXECUTE: case PAGE_EXECUTE_READ: return PROT_READ | PROT_EXEC;
    case PAGE_EXECUTE_READWRITE: return PROT_READ | PROT_WRITE | PROT_EXEC;
    default: return PROT_READ | PROT_WRITE;
    }
}
/* the reservations VirtualAlloc made, for MEM_RELEASE (which gives no size) */
static struct { uintptr_t base; size_t size; } regions[256];
static pthread_mutex_t regions_mu = PTHREAD_MUTEX_INITIALIZER;
/* guest pages given back so that they read as zero: MADV_REMOVE when the window is the memfd's (hos_main.c), as
   MADV_DONTNEED leaves a shared mapping's pages as they were; MADV_DONTNEED elsewhere */
static int discard_pages(void *p, size_t n) {
    typedef int (*DiscardFn)(void *, size_t);
    static DiscardFn fn;
    static int looked;
    if (!looked) fn = (DiscardFn)dlsym(RTLD_DEFAULT, "hos_posix_guest_discard"), looked = 1;
    if (fn && (uintptr_t)p >= GUEST_LO && (uintptr_t)p + n <= GUEST_HI) return fn(p, n);
    return madvise(p, n, MADV_DONTNEED);
}
void *VirtualAlloc(void *addr, SIZE_T size, DWORD type, DWORD protect) {
    const uintptr_t page = 4096;
    if (type & MEM_RESET) {   /* (the contents are not wanted any more: they read as zero after this) */
        const uintptr_t lo = ((uintptr_t)addr + page - 1) & ~(page - 1), hi = ((uintptr_t)addr + size) & ~(page - 1);
        if (hi > lo) discard_pages((void *)lo, hi - lo);
        return addr;
    }
    if (addr && (type & MEM_COMMIT) && !(type & MEM_RESERVE)) {
        /* (inside a reservation: made accessible; the guest window is accessible already) */
        if ((uintptr_t)addr >= GUEST_LO && (uintptr_t)addr + size <= GUEST_HI) return addr;
        const uintptr_t lo = (uintptr_t)addr & ~(page - 1), hi = ((uintptr_t)addr + size + page - 1) & ~(page - 1);
        if (mprotect((void *)lo, hi - lo, prot_of(protect))) { fail_errno(); return NULL; }
        return addr;
    }
    const int prot = (type & MEM_COMMIT) ? prot_of(protect) : PROT_NONE;
    void *p = mmap(addr, size, prot, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | (addr ? MAP_FIXED_NOREPLACE : 0), -1, 0);
    if (p == MAP_FAILED || (addr && p != addr)) {
        if (p != MAP_FAILED) munmap(p, size);
        last_error = ERROR_INVALID_PARAMETER;
        return NULL;
    }
    pthread_mutex_lock(&regions_mu);
    for (int i = 0; i < 256; i++)
        if (!regions[i].base) { regions[i].base = (uintptr_t)p, regions[i].size = size; break; }
    pthread_mutex_unlock(&regions_mu);
    return p;
}
BOOL VirtualFree(void *addr, SIZE_T size, DWORD type) {
    if (type & MEM_DECOMMIT) {   /* (kept mapped: decommitted memory reads as zero when touched again) */
        const uintptr_t lo = (uintptr_t)addr & ~(uintptr_t)4095, hi = ((uintptr_t)addr + size + 4095) & ~(uintptr_t)4095;
        return discard_pages((void *)lo, hi - lo) == 0 ? TRUE : fail_errno();
    }
    if (type & MEM_RELEASE) {
        pthread_mutex_lock(&regions_mu);
        for (int i = 0; i < 256; i++)
            if (regions[i].base == (uintptr_t)addr) {
                munmap(addr, regions[i].size);
                regions[i].base = 0;
                pthread_mutex_unlock(&regions_mu);
                return TRUE;
            }
        pthread_mutex_unlock(&regions_mu);
        last_error = ERROR_INVALID_PARAMETER;
        return FALSE;
    }
    return FALSE;
}
BOOL VirtualProtect(void *addr, SIZE_T size, DWORD protect, DWORD *old) {
    const uintptr_t lo = (uintptr_t)addr & ~(uintptr_t)4095, hi = ((uintptr_t)addr + size + 4095) & ~(uintptr_t)4095;
    if (old) *old = PAGE_READWRITE;
    return mprotect((void *)lo, hi - lo, prot_of(protect)) == 0 ? TRUE : fail_errno();
}
SIZE_T VirtualQuery(const void *addr, MEMORY_BASIC_INFORMATION *mbi, SIZE_T size) {
    if (size < sizeof *mbi) return 0;
    memset(mbi, 0, sizeof *mbi);
    const uintptr_t a = (uintptr_t)addr, page = a & ~(uintptr_t)4095;
    mbi->BaseAddress = (void *)page;
    if (a >= GUEST_LO && a < GUEST_HI) {
        mbi->AllocationBase = (void *)GUEST_LO;
        mbi->RegionSize = GUEST_HI - page;
        mbi->State = MEM_COMMIT, mbi->Protect = mbi->AllocationProtect = PAGE_READWRITE, mbi->Type = 0x20000;
        return sizeof *mbi;
    }
    /* (anything else: the kernel's map of the process) */
    FILE *f = fopen("/proc/self/maps", "r");
    char line[512];
    uintptr_t prev_end = 0;
    mbi->State = MEM_FREE, mbi->Protect = PAGE_NOACCESS, mbi->RegionSize = 4096;
    while (f && fgets(line, sizeof line, f)) {
        unsigned long lo, hi;
        char perms[8] = "";
        if (sscanf(line, "%lx-%lx %7s", &lo, &hi, perms) != 3) continue;
        if (a < lo) {
            mbi->RegionSize = lo - (page > prev_end ? page : prev_end);
            break;
        }
        prev_end = hi;
        if (a >= hi) continue;
        mbi->AllocationBase = (void *)lo;
        mbi->RegionSize = hi - page;
        const int r = perms[0] == 'r', w = perms[1] == 'w', x = perms[2] == 'x';
        mbi->State = r || w || x ? MEM_COMMIT : MEM_RESERVE;
        mbi->Protect = mbi->AllocationProtect = !r ? PAGE_NOACCESS : x ? (w ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ)
                                                                       : (w ? PAGE_READWRITE : PAGE_READONLY);
        mbi->Type = 0x20000;
        break;
    }
    if (f) fclose(f);
    return sizeof *mbi;
}
#endif

/* ---- modules ---- */
static void exe_path(char *out, size_t cap) {
#ifdef __PROSPERO__
    snprintf(out, cap, "%s", HOS_PS5_EXE);
#else
    const ssize_t n = readlink("/proc/self/exe", out, cap - 1);
    out[n > 0 ? n : 0] = 0;
#endif
}
/* a Windows library name, as the Linux one: SDL3.dll -> libSDL3.so.0, SDL2.dll -> libSDL2-2.0.so.0,
   vulkan-1.dll -> libvulkan.so.1, x.dll -> x.so */
static const char *linux_name(const char *base) {
    static const char *map[][2] = {{"sdl3.dll", "libSDL3.so.0"}, {"sdl2.dll", "libSDL2-2.0.so.0"},
                                   {"vulkan-1.dll", "libvulkan.so.1"}};
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (!strcasecmp(base, map[i][0])) return map[i][1];
    return NULL;
}
HMODULE LoadLibraryA(LPCSTR name) {
    if (!name) return NULL;
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s", name);
    for (char *p = path; *p; p++) if (*p == '\\') *p = '/';
    char *slash = strrchr(path, '/');
    char *base = slash ? slash + 1 : path;
    const char *mapped = linux_name(base);
    if (mapped) {
        snprintf(base, sizeof path - (size_t)(base - path), "%s", mapped);
    } else {
        const size_t n = strlen(path);
        if (n > 4 && !strcasecmp(path + n - 4, ".dll")) {
            if (!slash) return NULL;   /* (a Windows system library: kernel32, xinput, imm32 ...) */
            strcpy(path + n - 4, ".so");
        }
    }
    HMODULE m = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!m) last_error = ERROR_FILE_NOT_FOUND;
    return m;
}
BOOL FreeLibrary(HMODULE m) { return m && dlclose(m) == 0; }
FARPROC GetProcAddress(HMODULE m, LPCSTR name) { return m ? (FARPROC)dlsym(m, name) : NULL; }
HMODULE GetModuleHandleA(LPCSTR name) {
    if (!name) return dlopen(NULL, RTLD_NOW);
    return NULL;   /* (Windows' own modules: none here) */
}
BOOL GetModuleHandleExA(DWORD flags, LPCSTR name, HMODULE *out) {
    *out = NULL;
    if (!(flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS)) {
        *out = GetModuleHandleA(name);
        return *out != NULL;
    }
#ifdef __PROSPERO__   /* (one executable: every address is the eboot's, its base the crash offsets subtract) */
    uintptr_t hos_ps5_exe_base(void);
    *out = (HMODULE)hos_ps5_exe_base();
    return *out != NULL;
#endif
    Dl_info di;
    if (!dladdr(name, &di) || !di.dli_fbase) return FALSE;
    *out = di.dli_fbase;   /* (the module's base address, as an HMODULE is on Windows: crash offsets subtract it) */
    return TRUE;
}
DWORD GetModuleFileNameA(HMODULE m, LPSTR out, DWORD cap) {
    char path[PATH_MAX] = "";
#ifdef __PROSPERO__   /* (the back end and the game are one executable) */
    (void)m;
    exe_path(path, sizeof path);
#else
    Dl_info di;
    if (!m) {
        exe_path(path, sizeof path);
    } else if (dladdr(m, &di) && di.dli_fname) {
        Dl_info exe;
        if (dladdr((void *)&exe_path, &exe) && exe.dli_fbase == di.dli_fbase && strcmp(exe.dli_fname, di.dli_fname) &&
            !strchr(di.dli_fname, '/'))
            exe_path(path, sizeof path);
        else if (!realpath(di.dli_fname, path))
            snprintf(path, sizeof path, "%s", di.dli_fname);
        if (!path[0]) exe_path(path, sizeof path);
    } else {
        void *lm = NULL;   /* (a handle from LoadLibraryA) */
        if (dlinfo(m, RTLD_DI_LINKMAP, &lm) == 0 && lm) snprintf(path, sizeof path, "%s", ((const char **)lm)[1]);
    }
#endif
    if (!cap) return 0;
    const size_t n = strlen(path);
    const size_t k = n < cap - 1 ? n : cap - 1;
    memcpy(out, path, k);
    out[k] = 0;
    return (DWORD)k;
}
static void slashes(char *dst, size_t cap, const char *src) {
    snprintf(dst, cap, "%s", src);
    for (char *p = dst; *p; p++) if (*p == '\\') *p = '/';
}
BOOL SetCurrentDirectoryA(LPCSTR dir) {
    char p[PATH_MAX];
    slashes(p, sizeof p, dir);
    return chdir(p) == 0 ? TRUE : fail_errno();
}
DWORD GetCurrentDirectoryA(DWORD cap, LPSTR out) {
    char p[PATH_MAX];
    if (!getcwd(p, sizeof p)) return 0;
    const DWORD n = (DWORD)strlen(p);
    if (n + 1 > cap) return n + 1;
    memcpy(out, p, n + 1);
    return n;
}

/* ---- files ---- */
#ifdef __PROSPERO__   /* (a file of the romfs in the game's .nup: fd -1, nup its handle, pos where read() goes on) */
#include "../ps5/nup_romfs.h"
typedef struct { HObj h; int fd; int64_t nup; uint64_t pos; } HFile;
static HFile std_files[3] = {{{H_FILE}, 0, -1, 0}, {{H_FILE}, 1, -1, 0}, {{H_FILE}, 2, -1, 0}};
#define NUP_FILE(f) ((f)->fd < 0 && (f)->nup >= 0)
#else
typedef struct { HObj h; int fd; } HFile;
static HFile std_files[3] = {{{H_FILE}, 0}, {{H_FILE}, 1}, {{H_FILE}, 2}};
#endif
HANDLE GetStdHandle(DWORD which) {
    if (which == STD_INPUT_HANDLE) return &std_files[0];
    if (which == STD_OUTPUT_HANDLE) return &std_files[1];
    if (which == STD_ERROR_HANDLE) return &std_files[2];
    return INVALID_HANDLE_VALUE;
}
/* (PS5) file calls that take long are named in the log: a write to the title's folder was measured at ~60 ms a call, and a
   game thread that makes thirty of them is a two-second stall. IO_INNER(f) is f itself elsewhere. */
#ifdef __PROSPERO__
#define IO_INNER(f) f##_inner
#define IO_STATIC static
static double io_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}
static void io_note(const char *what, const char *name, long n, double t0) {
    const double d = io_ms() - t0;
    static _Atomic int told;
    if (d >= 30 && told++ < 400) fprintf(stderr, "[ps5] slow file call: %s %s%s%ld bytes took %.0f ms\n", what, name ? name : "", name ? " " : "", n, d);
}
static HANDLE CreateFileA_inner(LPCSTR, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
static BOOL ReadFile_inner(HANDLE, void *, DWORD, DWORD *, OVERLAPPED *);
static BOOL WriteFile_inner(HANDLE, const void *, DWORD, DWORD *, OVERLAPPED *);
static BOOL FlushFileBuffers_inner(HANDLE);
HANDLE CreateFileA(LPCSTR name, DWORD access, DWORD share, void *attr, DWORD disposition, DWORD flags, HANDLE tmpl) {
    const double t0 = io_ms();
    const HANDLE h = CreateFileA_inner(name, access, share, attr, disposition, flags, tmpl);
    io_note("open", name, 0, t0);
    return h;
}
BOOL ReadFile(HANDLE h, void *buf, DWORD n, DWORD *got, OVERLAPPED *ov) {
    const double t0 = io_ms();
    const BOOL r = ReadFile_inner(h, buf, n, got, ov);
    io_note("read", NULL, (long)n, t0);
    return r;
}
BOOL WriteFile(HANDLE h, const void *buf, DWORD n, DWORD *put, OVERLAPPED *ov) {
    const double t0 = io_ms();
    const BOOL r = WriteFile_inner(h, buf, n, put, ov);
    io_note("write", NULL, (long)n, t0);
    return r;
}
BOOL FlushFileBuffers(HANDLE h) {
    const double t0 = io_ms();
    const BOOL r = FlushFileBuffers_inner(h);
    io_note("flush", NULL, 0, t0);
    return r;
}
static
#else
#define IO_INNER(f) f
#define IO_STATIC
#endif
HANDLE IO_INNER(CreateFileA)(LPCSTR name, DWORD access, DWORD share, void *attr, DWORD disposition, DWORD flags, HANDLE tmpl) {
    (void)share, (void)attr, (void)flags, (void)tmpl;
    char path[PATH_MAX];
    slashes(path, sizeof path, name);
#ifdef __PROSPERO__
    if (nup_romfs_path(path)) {
        const int64_t n = (access & GENERIC_WRITE) ? -1 : nup_romfs_open_file(path);
        if (n < 0) { last_error = (access & GENERIC_WRITE) ? ERROR_ACCESS_DENIED : ERROR_FILE_NOT_FOUND; return INVALID_HANDLE_VALUE; }
        HFile *f = (HFile *)calloc(1, sizeof *f);
        f->h.type = H_FILE, f->fd = -1, f->nup = n;
        return f;
    }
#endif
    int o = (access & GENERIC_READ) && (access & GENERIC_WRITE) ? O_RDWR : (access & GENERIC_WRITE) ? O_WRONLY : O_RDONLY;
    switch (disposition) {
    case CREATE_NEW: o |= O_CREAT | O_EXCL; break;
    case CREATE_ALWAYS: o |= O_CREAT | O_TRUNC; break;
    case OPEN_ALWAYS: o |= O_CREAT; break;
    case TRUNCATE_EXISTING: o |= O_TRUNC; break;
    default: break;
    }
    const int fd = open(path, o | O_CLOEXEC, 0644);
    if (fd < 0) {
        last_error = errno == EEXIST ? ERROR_FILE_EXISTS : error_of(errno);
        return INVALID_HANDLE_VALUE;
    }
    struct stat st;
    if (fstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) {   /* (Windows opens no folder this way) */
        close(fd);
        last_error = ERROR_ACCESS_DENIED;
        return INVALID_HANDLE_VALUE;
    }
    HFile *f = (HFile *)calloc(1, sizeof *f);
    f->h.type = H_FILE, f->fd = fd;
#ifdef __PROSPERO__
    f->nup = -1;
#endif
    return f;
}
IO_STATIC BOOL IO_INNER(ReadFile)(HANDLE h, void *buf, DWORD n, DWORD *got, OVERLAPPED *ov) {
    if (got) *got = 0;
#ifdef __PROSPERO__   /* (a guest buffer the game never touched may be only reserved: the kernel's write into it would fail) */
    int hos_ps5_commit_now(void *addr, size_t size);
    hos_ps5_commit_now(buf, n);
#endif
    if (!is(h, H_FILE)) { last_error = ERROR_INVALID_HANDLE; return FALSE; }
    const int fd = ((HFile *)h)->fd;
    off_t at = ov ? (off_t)((uint64_t)ov->OffsetHigh << 32 | ov->Offset) : 0;
#ifdef __PROSPERO__
    if (NUP_FILE((HFile *)h)) {
        HFile *f = (HFile *)h;
        const int64_t r = nup_romfs_pread(f->nup, buf, n, ov ? (uint64_t)at : f->pos);
        if (r < 0) { last_error = ERROR_INVALID_PARAMETER; return FALSE; }
        if (!ov) f->pos += (uint64_t)r;
        if (got) *got = (DWORD)r;
        if (ov && !r && n) { last_error = ERROR_HANDLE_EOF; return FALSE; }
        return TRUE;
    }
#endif
    DWORD done = 0;
    while (done < n) {
        const ssize_t r = ov ? pread(fd, (char *)buf + done, n - done, at + done) : read(fd, (char *)buf + done, n - done);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) { if (got) *got = done; return fail_errno(); }
        if (!r) break;
        done += (DWORD)r;
    }
    if (got) *got = done;
    if (ov && !done && n) { last_error = ERROR_HANDLE_EOF; return FALSE; }
    return TRUE;
}
IO_STATIC BOOL IO_INNER(WriteFile)(HANDLE h, const void *buf, DWORD n, DWORD *put, OVERLAPPED *ov) {
    if (put) *put = 0;
#ifdef __PROSPERO__
    int hos_ps5_commit_now(void *addr, size_t size);
    hos_ps5_commit_now((void *)buf, n);
#endif
    if (!is(h, H_FILE)) { last_error = ERROR_INVALID_HANDLE; return FALSE; }
    const int fd = ((HFile *)h)->fd;
    off_t at = ov ? (off_t)((uint64_t)ov->OffsetHigh << 32 | ov->Offset) : 0;
    DWORD done = 0;
    while (done < n) {
        const ssize_t r = ov ? pwrite(fd, (const char *)buf + done, n - done, at + done) : write(fd, (const char *)buf + done, n - done);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) { if (put) *put = done; return fail_errno(); }
        done += (DWORD)r;
    }
    if (put) *put = done;
    return TRUE;
}
BOOL SetFilePointerEx(HANDLE h, LARGE_INTEGER dist, LARGE_INTEGER *pos, DWORD method) {
    if (!is(h, H_FILE)) { last_error = ERROR_INVALID_HANDLE; return FALSE; }
#ifdef __PROSPERO__
    if (NUP_FILE((HFile *)h)) {
        HFile *f = (HFile *)h;
        const int64_t base = method == FILE_END ? (int64_t)nup_romfs_size(f->nup) : method == FILE_CURRENT ? (int64_t)f->pos : 0;
        if (base + dist.QuadPart < 0) { last_error = ERROR_INVALID_PARAMETER; return FALSE; }
        f->pos = (uint64_t)(base + dist.QuadPart);
        if (pos) pos->QuadPart = (LONGLONG)f->pos;
        return TRUE;
    }
#endif
    const off_t r = lseek(((HFile *)h)->fd, (off_t)dist.QuadPart, method == FILE_END ? SEEK_END : method == FILE_CURRENT ? SEEK_CUR : SEEK_SET);
    if (r < 0) return fail_errno();
    if (pos) pos->QuadPart = r;
    return TRUE;
}
BOOL GetFileSizeEx(HANDLE h, LARGE_INTEGER *size) {
#ifdef __PROSPERO__
    if (is(h, H_FILE) && NUP_FILE((HFile *)h)) { size->QuadPart = (LONGLONG)nup_romfs_size(((HFile *)h)->nup); return TRUE; }
#endif
    struct stat st;
    if (!is(h, H_FILE) || fstat(((HFile *)h)->fd, &st)) { last_error = ERROR_INVALID_HANDLE; return FALSE; }
    size->QuadPart = st.st_size;
    return TRUE;
}
DWORD GetFileSize(HANDLE h, DWORD *high) {
    LARGE_INTEGER s;
    if (!GetFileSizeEx(h, &s)) return INVALID_FILE_SIZE;
    if (high) *high = (DWORD)((uint64_t)s.QuadPart >> 32);
    return (DWORD)s.QuadPart;
}
BOOL SetEndOfFile(HANDLE h) {
    if (!is(h, H_FILE)) { last_error = ERROR_INVALID_HANDLE; return FALSE; }
    const int fd = ((HFile *)h)->fd;
    const off_t at = lseek(fd, 0, SEEK_CUR);
    return at >= 0 && ftruncate(fd, at) == 0 ? TRUE : fail_errno();
}
IO_STATIC BOOL IO_INNER(FlushFileBuffers)(HANDLE h) {
    if (!is(h, H_FILE)) { last_error = ERROR_INVALID_HANDLE; return FALSE; }
    return fdatasync(((HFile *)h)->fd) == 0 || errno == EINVAL ? TRUE : fail_errno();
}
DWORD GetFileType(HANDLE h) {
#ifdef __PROSPERO__
    if (is(h, H_FILE) && NUP_FILE((HFile *)h)) return FILE_TYPE_DISK;
#endif
    struct stat st;
    if (!is(h, H_FILE) || fstat(((HFile *)h)->fd, &st)) return FILE_TYPE_UNKNOWN;
    if (S_ISREG(st.st_mode)) return FILE_TYPE_DISK;
    if (S_ISFIFO(st.st_mode)) return FILE_TYPE_PIPE;
    /* (a terminal is the console; /dev/null and the journal's socket, where a desktop launcher sends it, count as none) */
    if (S_ISCHR(st.st_mode)) return isatty(((HFile *)h)->fd) ? FILE_TYPE_CHAR : FILE_TYPE_UNKNOWN;
    return FILE_TYPE_UNKNOWN;
}
BOOL GetConsoleMode(HANDLE h, DWORD *mode) {
    if (!is(h, H_FILE) || !isatty(((HFile *)h)->fd)) return FALSE;
    if (mode) *mode = 0;
    return TRUE;
}
DWORD GetFileAttributesA(LPCSTR name) {
    char path[PATH_MAX];
    slashes(path, sizeof path, name);
#ifdef __PROSPERO__
    if (nup_romfs_path(path)) {
        int dir = 0;
        if (nup_romfs_find(path, &dir, NULL) < 0) { last_error = ERROR_FILE_NOT_FOUND; return INVALID_FILE_ATTRIBUTES; }
        return dir ? FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY : FILE_ATTRIBUTE_READONLY;
    }
#endif
    struct stat st;
    if (stat(path, &st)) {
        fail_errno();
        return INVALID_FILE_ATTRIBUTES;
    }
    DWORD at = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    struct stat link;   /* (hookshot: a symbolic link is marked as Windows marks one, a reparse point; find_next too) */
    if (lstat(path, &link) == 0 && S_ISLNK(link.st_mode)) at |= FILE_ATTRIBUTE_REPARSE_POINT;
    return at;
}
BOOL CreateDirectoryA(LPCSTR name, void *attr) {
    (void)attr;
    char path[PATH_MAX];
    slashes(path, sizeof path, name);
    if (mkdir(path, 0755) == 0) return TRUE;
    last_error = errno == EEXIST ? ERROR_ALREADY_EXISTS : errno == ENOENT ? ERROR_PATH_NOT_FOUND : error_of(errno);
    return FALSE;
}
BOOL RemoveDirectoryA(LPCSTR name) {
    char path[PATH_MAX];
    slashes(path, sizeof path, name);
    return rmdir(path) == 0 ? TRUE : fail_errno();
}
BOOL DeleteFileA(LPCSTR name) {
    char path[PATH_MAX];
    slashes(path, sizeof path, name);
    return unlink(path) == 0 ? TRUE : fail_errno();
}
BOOL MoveFileExA(LPCSTR from, LPCSTR to, DWORD flags) {
    char a[PATH_MAX], b[PATH_MAX];
    slashes(a, sizeof a, from);
    slashes(b, sizeof b, to);
    struct stat st;
    if (!(flags & MOVEFILE_REPLACE_EXISTING) && stat(b, &st) == 0) { last_error = ERROR_ALREADY_EXISTS; return FALSE; }
    return rename(a, b) == 0 ? TRUE : fail_errno();
}
BOOL MoveFileA(LPCSTR from, LPCSTR to) { return MoveFileExA(from, to, 0); }
BOOL CopyFileA(LPCSTR from, LPCSTR to, BOOL fail_if_exists) {
    char a[PATH_MAX], b[PATH_MAX];
    slashes(a, sizeof a, from);
    slashes(b, sizeof b, to);
    const int in = open(a, O_RDONLY | O_CLOEXEC);
    if (in < 0) return fail_errno();
    const int out = open(b, O_WRONLY | O_CREAT | O_CLOEXEC | (fail_if_exists ? O_EXCL : O_TRUNC), 0644);
    if (out < 0) { close(in); return fail_errno(); }
    char buf[1 << 16];
    ssize_t n;
    BOOL ok = TRUE;
    while ((n = read(in, buf, sizeof buf)) > 0)
        if (write(out, buf, (size_t)n) != n) { ok = fail_errno(); break; }
    close(in), close(out);
    return ok;
}
#ifdef __PROSPERO__   /* (a folder of the .nup's romfs: d NULL, nup_dir its node, cursor the next child) */
typedef struct { HObj h; DIR *d; char dir[PATH_MAX], pat[256]; int64_t nup_dir; uint32_t cursor; } HFind;
#else
typedef struct { HObj h; DIR *d; char dir[PATH_MAX], pat[256]; } HFind;
#endif
static BOOL find_next(HFind *f, WIN32_FIND_DATAA *fd) {
#ifdef __PROSPERO__
    if (!f->d) {
        const char *name;
        size_t len;
        int dir;
        uint64_t size;
        while (nup_romfs_dir_next(f->nup_dir, &f->cursor, &name, &len, &dir, &size)) {
            char n[260];
            snprintf(n, sizeof n, "%.*s", (int)len, name);
            if (fnmatch(f->pat, n, FNM_CASEFOLD) && strcmp(f->pat, "*.*")) continue;
            memset(fd, 0, sizeof *fd);
            snprintf(fd->cFileName, sizeof fd->cFileName, "%s", n);
            fd->dwFileAttributes = dir ? FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY : FILE_ATTRIBUTE_READONLY;
            fd->nFileSizeLow = (DWORD)size, fd->nFileSizeHigh = (DWORD)(size >> 32);
            return TRUE;
        }
        last_error = ERROR_NO_MORE_FILES;
        return FALSE;
    }
#endif
    struct dirent *e;
    while ((e = readdir(f->d)) != NULL) {
        if (fnmatch(f->pat, e->d_name, FNM_CASEFOLD) && strcmp(f->pat, "*.*")) continue;
        memset(fd, 0, sizeof *fd);
        snprintf(fd->cFileName, sizeof fd->cFileName, "%s", e->d_name);
        char full[PATH_MAX + 260];
        snprintf(full, sizeof full, "%s/%s", f->dir, e->d_name);
        struct stat st;
        if (stat(full, &st) == 0) {
            fd->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
            fd->nFileSizeLow = (DWORD)st.st_size, fd->nFileSizeHigh = (DWORD)((uint64_t)st.st_size >> 32);
        } else {
            fd->dwFileAttributes = e->d_type == DT_DIR ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        }
        struct stat link;   /* (hookshot: a symbolic link is marked as Windows marks one, a reparse point) */
        if (lstat(full, &link) == 0 && S_ISLNK(link.st_mode)) fd->dwFileAttributes |= FILE_ATTRIBUTE_REPARSE_POINT;
        return TRUE;
    }
    last_error = ERROR_NO_MORE_FILES;
    return FALSE;
}
HANDLE FindFirstFileA(LPCSTR pattern, WIN32_FIND_DATAA *fd) {
    HFind *f = (HFind *)calloc(1, sizeof *f);
    f->h.type = H_FIND;
    slashes(f->dir, sizeof f->dir, pattern);
    char *slash = strrchr(f->dir, '/');
    if (slash) {
        snprintf(f->pat, sizeof f->pat, "%s", slash + 1);
        if (slash == f->dir) slash[1] = 0;
        else *slash = 0;
    } else {
        snprintf(f->pat, sizeof f->pat, "%s", f->dir);
        strcpy(f->dir, ".");
    }
#ifdef __PROSPERO__
    if (nup_romfs_path(f->dir)) {
        int dir = 0;
        f->nup_dir = nup_romfs_find(f->dir, &dir, NULL);
        if (f->nup_dir < 0 || !dir) { last_error = ERROR_PATH_NOT_FOUND; free(f); return INVALID_HANDLE_VALUE; }
        if (!find_next(f, fd)) { free(f); last_error = ERROR_FILE_NOT_FOUND; return INVALID_HANDLE_VALUE; }
        return f;
    }
#endif
    f->d = opendir(f->dir);
    if (!f->d) {
        last_error = errno == ENOENT ? ERROR_PATH_NOT_FOUND : error_of(errno);
        free(f);
        return INVALID_HANDLE_VALUE;
    }
    if (!find_next(f, fd)) {
        closedir(f->d);
        free(f);
        last_error = ERROR_FILE_NOT_FOUND;
        return INVALID_HANDLE_VALUE;
    }
    return f;
}
BOOL FindNextFileA(HANDLE h, WIN32_FIND_DATAA *fd) {
    if (!is(h, H_FIND)) { last_error = ERROR_INVALID_HANDLE; return FALSE; }
    return find_next((HFind *)h, fd);
}
BOOL FindClose(HANDLE h) {
    if (!is(h, H_FIND)) return FALSE;
    HFind *f = (HFind *)h;
    if (f->d) closedir(f->d);
    f->h.type = 0;
    free(f);
    return TRUE;
}

BOOL CloseHandle(HANDLE h) {
    if (!h || h == INVALID_HANDLE_VALUE || h == PSEUDO_THREAD || h == GetCurrentProcess()) return TRUE;
    if (h >= (HANDLE)&std_files[0] && h <= (HANDLE)&std_files[2]) return TRUE;
    HObj *o = (HObj *)h;
    switch (o->type) {
    case H_THREAD: thread_unref((HThread *)h); return TRUE;
    case H_EVENT:
    case H_SEMA: {
        HEvent *e = (HEvent *)h;
        e->h.type = 0;
        pthread_mutex_destroy(&e->mu);
        pthread_cond_destroy(&e->cv);
        free(e);
        return TRUE;
    }
    case H_TIMER: o->type = 0; free(o); return TRUE;
#ifdef __PROSPERO__
    case H_FILE:
        if (NUP_FILE((HFile *)h)) nup_romfs_close(((HFile *)h)->nup);
        else close(((HFile *)h)->fd);
        o->type = 0, free(o);
        return TRUE;
#else
    case H_FILE: close(((HFile *)h)->fd); o->type = 0; free(o); return TRUE;
#endif
    case H_FIND: return FindClose(h);
    }
    last_error = ERROR_INVALID_HANDLE;
    return FALSE;
}

/* ---- exceptions ---- */
static PVECTORED_EXCEPTION_HANDLER handlers[16];
static int nhandlers;
static LPTOP_LEVEL_EXCEPTION_FILTER unhandled;
static struct sigaction previous[32];
static pthread_mutex_t handlers_mu = PTHREAD_MUTEX_INITIALIZER;

#ifdef __PROSPERO__   /* (FreeBSD's machine context, as the console lays it out: the SDK fork's sys/_ucontext.h) */
#include <ps5platform/context.h>
static void context_from(CONTEXT *c, const ucontext_t *uc) {
    const mcontext_t *m = &uc->uc_mcontext;
    c->Rax = (DWORD64)m->mc_rax, c->Rcx = (DWORD64)m->mc_rcx, c->Rdx = (DWORD64)m->mc_rdx, c->Rbx = (DWORD64)m->mc_rbx;
    c->Rsp = (DWORD64)m->mc_rsp, c->Rbp = (DWORD64)m->mc_rbp, c->Rsi = (DWORD64)m->mc_rsi, c->Rdi = (DWORD64)m->mc_rdi;
    c->R8 = (DWORD64)m->mc_r8, c->R9 = (DWORD64)m->mc_r9, c->R10 = (DWORD64)m->mc_r10, c->R11 = (DWORD64)m->mc_r11;
    c->R12 = (DWORD64)m->mc_r12, c->R13 = (DWORD64)m->mc_r13, c->R14 = (DWORD64)m->mc_r14, c->R15 = (DWORD64)m->mc_r15;
    c->Rip = (DWORD64)m->mc_rip, c->EFlags = (DWORD)m->mc_rflags, c->ContextFlags = CONTEXT_FULL;
}
static void context_to(ucontext_t *uc, const CONTEXT *c) {
    mcontext_t *m = &uc->uc_mcontext;
    m->mc_rax = (register_t)c->Rax, m->mc_rcx = (register_t)c->Rcx, m->mc_rdx = (register_t)c->Rdx, m->mc_rbx = (register_t)c->Rbx;
    m->mc_rsp = (register_t)c->Rsp, m->mc_rbp = (register_t)c->Rbp, m->mc_rsi = (register_t)c->Rsi, m->mc_rdi = (register_t)c->Rdi;
    m->mc_r8 = (register_t)c->R8, m->mc_r9 = (register_t)c->R9, m->mc_r10 = (register_t)c->R10, m->mc_r11 = (register_t)c->R11;
    m->mc_r12 = (register_t)c->R12, m->mc_r13 = (register_t)c->R13, m->mc_r14 = (register_t)c->R14, m->mc_r15 = (register_t)c->R15;
    m->mc_rip = (register_t)c->Rip;
}
#define HOS_FAULT_WRITE(uc) (((uc)->uc_mcontext.mc_err & 2) != 0)
#else
#define HOS_FAULT_WRITE(uc) (((uc)->uc_mcontext.gregs[REG_ERR] & 2) != 0)
static void context_from(CONTEXT *c, const ucontext_t *uc) {
    const greg_t *g = uc->uc_mcontext.gregs;
    c->Rax = (DWORD64)g[REG_RAX], c->Rcx = (DWORD64)g[REG_RCX], c->Rdx = (DWORD64)g[REG_RDX], c->Rbx = (DWORD64)g[REG_RBX];
    c->Rsp = (DWORD64)g[REG_RSP], c->Rbp = (DWORD64)g[REG_RBP], c->Rsi = (DWORD64)g[REG_RSI], c->Rdi = (DWORD64)g[REG_RDI];
    c->R8 = (DWORD64)g[REG_R8], c->R9 = (DWORD64)g[REG_R9], c->R10 = (DWORD64)g[REG_R10], c->R11 = (DWORD64)g[REG_R11];
    c->R12 = (DWORD64)g[REG_R12], c->R13 = (DWORD64)g[REG_R13], c->R14 = (DWORD64)g[REG_R14], c->R15 = (DWORD64)g[REG_R15];
    c->Rip = (DWORD64)g[REG_RIP], c->EFlags = (DWORD)g[REG_EFL], c->ContextFlags = CONTEXT_FULL;
}
static void context_to(ucontext_t *uc, const CONTEXT *c) {
    greg_t *g = uc->uc_mcontext.gregs;
    g[REG_RAX] = (greg_t)c->Rax, g[REG_RCX] = (greg_t)c->Rcx, g[REG_RDX] = (greg_t)c->Rdx, g[REG_RBX] = (greg_t)c->Rbx;
    g[REG_RSP] = (greg_t)c->Rsp, g[REG_RBP] = (greg_t)c->Rbp, g[REG_RSI] = (greg_t)c->Rsi, g[REG_RDI] = (greg_t)c->Rdi;
    g[REG_R8] = (greg_t)c->R8, g[REG_R9] = (greg_t)c->R9, g[REG_R10] = (greg_t)c->R10, g[REG_R11] = (greg_t)c->R11;
    g[REG_R12] = (greg_t)c->R12, g[REG_R13] = (greg_t)c->R13, g[REG_R14] = (greg_t)c->R14, g[REG_R15] = (greg_t)c->R15;
    g[REG_RIP] = (greg_t)c->Rip;
}
#endif
#ifdef __PROSPERO__
/* (PS5) A fault while this thread is still handling one (the crash report allocating thread-local storage did, and the
   handler recursed until the stack ran out): both go to klog at once, with no allocation and no thread-local storage,
   and the second is left to the console's own crash report, which then names the first in klog beside it */
int sceKernelDebugOutText(int channel, const char *text);
uintptr_t hos_ps5_exe_base(void);
static volatile long fault_tid;
static volatile uintptr_t fault_rip, fault_addr;
static volatile int fault_sig;
static int fault_nested(int sig, const siginfo_t *si, const ucontext_t *uc) {
    const long me = (long)hos_ps5_tid();
    const uintptr_t rip = (uintptr_t)uc->uc_mcontext.mc_rip, base = hos_ps5_exe_base();
    if (__atomic_load_n(&fault_tid, __ATOMIC_ACQUIRE) != me) {
        long none = 0;
        if (__atomic_compare_exchange_n(&fault_tid, &none, me, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            fault_sig = sig, fault_rip = rip, fault_addr = (uintptr_t)si->si_addr;
        return 0;
    }
    char line[256];
    snprintf(line, sizeof line, "[ps5] fault in the fault handler: first signal %d at eboot+%#lx address %#lx, then signal %d "
             "at eboot+%#lx address %#lx\n", fault_sig, (unsigned long)(fault_rip - base), (unsigned long)fault_addr, sig,
             (unsigned long)(rip - base), (unsigned long)si->si_addr);
    sceKernelDebugOutText(0, line);
    signal(sig, SIG_DFL);
    return 1;
}
static void fault_done(void) {
    long me = (long)hos_ps5_tid();
    __atomic_compare_exchange_n(&fault_tid, &me, 0, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}
struct fault_scope;
static void fault_scope_end(void *unused) { (void)unused; fault_done(); }   /* (every return of on_signal) */
#endif
static void on_signal(int sig, siginfo_t *si, void *uctx) {
    ucontext_t *uc = (ucontext_t *)uctx;
#ifdef __PROSPERO__
    /* (a page-not-present fault on a guest unit another thread backed meanwhile: committed now, so the access is retried. Its
       own commit_on_touch would find the unit committed and take the fault for a crash) */
    int hos_ps5_unit_backed(uintptr_t addr), hos_ps5_stack_fault(uintptr_t addr);
    if (sig == SIGSEGV && !(uc->uc_mcontext.mc_err & 1) && hos_ps5_unit_backed((uintptr_t)si->si_addr)) return;
    /* (a stack growing into its reserved part: backed now, host/ps5/win32_ps5.c) */
    if (sig == SIGSEGV && !(uc->uc_mcontext.mc_err & 1) && hos_ps5_stack_fault((uintptr_t)si->si_addr)) return;
    if (fault_nested(sig, si, uc)) return;
    struct fault_scope { int unused; } scope __attribute__((cleanup(fault_scope_end))) = {0};
#endif
    EXCEPTION_RECORD rec;
    memset(&rec, 0, sizeof rec);
    CONTEXT ctx;
    context_from(&ctx, uc);
    rec.ExceptionCode = sig == SIGSEGV || sig == SIGBUS ? EXCEPTION_ACCESS_VIOLATION
                      : sig == SIGILL ? EXCEPTION_ILLEGAL_INSTRUCTION
                      : sig == SIGFPE ? EXCEPTION_INT_DIVIDE_BY_ZERO : EXCEPTION_BREAKPOINT;
    rec.ExceptionAddress = (void *)(uintptr_t)ctx.Rip;
    if (sig == SIGSEGV || sig == SIGBUS) {
        rec.NumberParameters = 2;
        rec.ExceptionInformation[0] = HOS_FAULT_WRITE(uc) ? 1 : 0;   /* (1: a write) */
        rec.ExceptionInformation[1] = (ULONG_PTR)si->si_addr;
    }
    EXCEPTION_POINTERS ep = {&rec, &ctx};
    for (int i = 0; i < nhandlers; i++)
        if (handlers[i] && handlers[i](&ep) == EXCEPTION_CONTINUE_EXECUTION) {
            context_to(uc, &ctx);
            return;
        }
#ifdef __PROSPERO__
    /* (the unit backed by another thread while this one's handlers ran: they found it committed and took the fault for
       not theirs. Retried, as at the top) */
    if (sig == SIGSEGV && !(uc->uc_mcontext.mc_err & 1) && hos_ps5_unit_backed((uintptr_t)si->si_addr)) return;
#endif
    if (unhandled && unhandled(&ep) == EXCEPTION_CONTINUE_EXECUTION) {
        context_to(uc, &ctx);
        return;
    }
#ifdef __PROSPERO__
    {   /* (said at once: the crash report's own lines go through a pipe the process's end can outrun) */
        void hos_ps5_klog_now(const char *fmt, ...);
        uintptr_t hos_ps5_exe_base(void);
        hos_ps5_klog_now("[ps5] unhandled signal %d at eboot+%#lx, address %#lx (%s, mc_err %#lx)", sig,
                         (unsigned long)((uintptr_t)uc->uc_mcontext.mc_rip - hos_ps5_exe_base()), (unsigned long)si->si_addr,
                         HOS_FAULT_WRITE(uc) ? "write" : "read", (unsigned long)uc->uc_mcontext.mc_err);
    }
#endif
    /* not ours: whoever had the signal before, else the default (the fault comes again and ends the process) */
    const struct sigaction *p = &previous[sig];
    if ((p->sa_flags & SA_SIGINFO) && p->sa_sigaction) {
        p->sa_sigaction(sig, si, uctx);
        return;
    }
    if (p->sa_handler != SIG_IGN && p->sa_handler != SIG_DFL && p->sa_handler) {
        p->sa_handler(sig);
        return;
    }
    signal(sig, SIG_DFL);
}
static void install_signals(void) {
    static int done;
    if (done) return;
    done = 1;
    const int sigs[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE};
    for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++) {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_sigaction = on_signal;
#ifdef __PROSPERO__
        sa.sa_flags = SA_SIGINFO | SA_NODEFER | SA_ONSTACK;   /* (on the thread's alternate stack: a stack fault leaves no room) */
#else
        sa.sa_flags = SA_SIGINFO | SA_NODEFER;
#endif
        sigemptyset(&sa.sa_mask);
        sigaction(sigs[i], &sa, &previous[sigs[i]]);
    }
}
void *AddVectoredExceptionHandler(ULONG first, PVECTORED_EXCEPTION_HANDLER h) {
    pthread_mutex_lock(&handlers_mu);
    install_signals();
    if (nhandlers < 16) {
        if (first) {
            memmove(&handlers[1], &handlers[0], sizeof handlers[0] * (size_t)nhandlers);
            handlers[0] = h;
        } else {
            handlers[nhandlers] = h;
        }
        nhandlers++;
    }
    pthread_mutex_unlock(&handlers_mu);
    return (void *)h;
}
ULONG RemoveVectoredExceptionHandler(void *h) {
    pthread_mutex_lock(&handlers_mu);
    for (int i = 0; i < nhandlers; i++)
        if ((void *)handlers[i] == h) {
            memmove(&handlers[i], &handlers[i + 1], sizeof handlers[0] * (size_t)(nhandlers - i - 1));
            nhandlers--;
            pthread_mutex_unlock(&handlers_mu);
            return 1;
        }
    pthread_mutex_unlock(&handlers_mu);
    return 0;
}
LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER f) {
    pthread_mutex_lock(&handlers_mu);
    install_signals();
    LPTOP_LEVEL_EXCEPTION_FILTER old = unhandled;
    unhandled = f;
    pthread_mutex_unlock(&handlers_mu);
    return old;
}

/* ---- the window's state, kept by display_sdl.c in the executable (found by name from the Vulkan back end) ---- */
static volatile uint8_t no_keys[256];
static volatile uint8_t *keys_table(void) {
    static volatile uint8_t *keys;
    if (!keys) {
        volatile uint8_t *k = (volatile uint8_t *)dlsym(RTLD_DEFAULT, "hos_posix_keys");
        keys = k ? k : no_keys;
    }
    return keys;
}
SHORT GetAsyncKeyState(int vk) { return keys_table()[vk & 255] ? (SHORT)0x8000 : 0; }
SHORT GetKeyState(int vk) { return GetAsyncKeyState(vk); }
static volatile int *window_state(void) {   /* [0] focused, [1] screen width, [2] screen height, [3] fullscreen */
    static volatile int none[4];
    static volatile int *s;
    if (!s) {
        volatile int *k = (volatile int *)dlsym(RTLD_DEFAULT, "hos_posix_window");
        s = k ? k : none;
    }
    return s;
}
int GetSystemMetrics(int index) {
    if (index == SM_CXSCREEN) return window_state()[1];
    if (index == SM_CYSCREEN) return window_state()[2];
    return 0;
}
HWND GetForegroundWindow(void) { return window_state()[0] ? (HWND)(uintptr_t)1 : NULL; }
BOOL PostMessageA(HWND w, UINT msg, WPARAM wp, LPARAM lp) {   /* (to the window's thread: display_sdl.c) */
    (void)w;
    typedef void (*PostFn)(uint32_t, uintptr_t, intptr_t);
    static PostFn post;
    if (!post) post = (PostFn)dlsym(RTLD_DEFAULT, "hos_posix_post_message");
    if (!post) return FALSE;
    post(msg, wp, lp);
    return TRUE;
}
LONG GetWindowLongA(HWND w, int index) {
    (void)w;
    return index == GWL_STYLE && !window_state()[3] ? WS_CAPTION : 0;
}
DWORD GetWindowThreadProcessId(HWND w, DWORD *pid) {
    if (pid) *pid = w ? (DWORD)getpid() : 0;
    return w ? GetCurrentThreadId() : 0;
}
int MessageBoxA(HWND w, LPCSTR text, LPCSTR caption, UINT type) {
    (void)w, (void)type;
    fprintf(stderr, "%s: %s\n", caption ? caption : "Hookshot", text ? text : "");
    typedef int (*ShowFn)(uint32_t, const char *, const char *, void *);   /* (SDL3's and SDL2's alike; its result unread) */
    void *sdl = dlopen("libSDL3.so.0", RTLD_NOW | RTLD_NOLOAD);
    if (!sdl) sdl = dlopen("libSDL2-2.0.so.0", RTLD_NOW | RTLD_NOLOAD);
    ShowFn show = sdl ? (ShowFn)dlsym(sdl, "SDL_ShowSimpleMessageBox") : NULL;
    if (show) show(0x10, caption ? caption : "Hookshot", text ? text : "", NULL);
    return 1;
}

/* ---- waveOut on SDL's audio (host/hos_sdl.c): a callback takes the written blocks in order and marks them done ---- */
/* (the executable's: this file is in the Vulkan back end too, which plays no sound and has no hos_sdl.c) */
int hos_sdl_audio_open(int freq, int channels, int s16, void (*pull)(void *user, uint8_t *out, int len), void *user)
    __attribute__((weak));
typedef struct {
    pthread_mutex_t mu;
    WAVEHDR *q[64];
    int head, count;
    DWORD pos;
} WaveOut;
static void wave_pull(void *user, uint8_t *out, int len) {
    WaveOut *w = (WaveOut *)user;
    pthread_mutex_lock(&w->mu);
    while (len > 0 && w->count) {
        WAVEHDR *h = w->q[w->head];
        DWORD k = h->dwBufferLength - w->pos;
        if (k > (DWORD)len) k = (DWORD)len;
        memcpy(out, h->lpData + w->pos, k);
        out += k, len -= (int)k, w->pos += k;
        if (w->pos >= h->dwBufferLength) {
            __atomic_store_n(&h->dwFlags, (h->dwFlags & ~(DWORD)WHDR_INQUEUE) | WHDR_DONE, __ATOMIC_RELEASE);
            w->head = (w->head + 1) % 64, w->count--, w->pos = 0;
        }
    }
    pthread_mutex_unlock(&w->mu);
    if (len > 0) memset(out, 0, (size_t)len);   /* (behind: silence) */
}
MMRESULT waveOutOpen(HWAVEOUT *out, UINT device, const WAVEFORMATEX *f, DWORD_PTR cb, DWORD_PTR inst, DWORD flags) {
    (void)device, (void)cb, (void)inst, (void)flags;
    /* (the same SDL as the window's and the controllers', beside the executable first: hos_sdl.c) */
    if (!hos_sdl_audio_open) return MMSYSERR_ERROR;
    WaveOut *w = (WaveOut *)calloc(1, sizeof *w);
    pthread_mutex_init(&w->mu, NULL);
    if (!hos_sdl_audio_open((int)f->nSamplesPerSec, (int)f->nChannels, f->wBitsPerSample == 16, wave_pull, w)) {
        pthread_mutex_destroy(&w->mu);
        free(w);
        return MMSYSERR_ERROR;
    }
    *out = w;
    return MMSYSERR_NOERROR;
}
MMRESULT waveOutPrepareHeader(HWAVEOUT w, WAVEHDR *h, UINT size) { (void)w, (void)size; h->dwFlags |= WHDR_PREPARED; return 0; }
MMRESULT waveOutUnprepareHeader(HWAVEOUT w, WAVEHDR *h, UINT size) { (void)w, (void)size; h->dwFlags &= ~(DWORD)WHDR_PREPARED; return 0; }
MMRESULT waveOutWrite(HWAVEOUT hw, WAVEHDR *h, UINT size) {
    (void)size;
    WaveOut *w = (WaveOut *)hw;
    pthread_mutex_lock(&w->mu);
    if (w->count == 64) { pthread_mutex_unlock(&w->mu); return MMSYSERR_ERROR; }
    h->dwFlags = (h->dwFlags & ~(DWORD)WHDR_DONE) | WHDR_INQUEUE;
    w->q[(w->head + w->count) % 64] = h;
    w->count++;
    pthread_mutex_unlock(&w->mu);
    return MMSYSERR_NOERROR;
}
MMRESULT waveOutReset(HWAVEOUT hw) {
    WaveOut *w = (WaveOut *)hw;
    pthread_mutex_lock(&w->mu);
    while (w->count) {
        w->q[w->head]->dwFlags = (w->q[w->head]->dwFlags & ~(DWORD)WHDR_INQUEUE) | WHDR_DONE;
        w->head = (w->head + 1) % 64, w->count--;
    }
    w->pos = 0;
    pthread_mutex_unlock(&w->mu);
    return 0;
}
MMRESULT waveOutClose(HWAVEOUT hw) { waveOutReset(hw); return 0; }
