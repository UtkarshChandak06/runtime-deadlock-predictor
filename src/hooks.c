/* Mutex wrappers: snapshot and analyze before acquisition; track only success. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* RTLD_NEXT, syscall() (normally set via CPPFLAGS) */
#endif

#include "rdp/internal.h" /* rdp_log */
#include "rdp/tracker.h"  /* held-lock tracker (Phase 2) */
#include "rdp/report.h"
#include "rdp/graph.h"    /* lock-order graph + cycle detection (Phase 3) */

#include <dlfcn.h>      /* dlsym, RTLD_NEXT, dlerror */
#include <errno.h>      /* errno, ENOSYS */
#include <pthread.h>    /* pthread_mutex_t, pthread_once */
#include <signal.h>
#include <stdatomic.h>
#include <time.h>

#include <unistd.h>     /* syscall */
#include <sys/syscall.h> /* SYS_gettid */

/* ------------------------------------------------------------------ */
/* Real (original) function pointers, resolved exactly once.          */
/* ------------------------------------------------------------------ */

typedef int (*real_lock_fn)(pthread_mutex_t *mutex);
typedef int (*real_unlock_fn)(pthread_mutex_t *mutex);

static real_lock_fn real_lock;
static real_unlock_fn real_unlock;
static pthread_once_t resolver_once = PTHREAD_ONCE_INIT;
/* 0 = unresolved, 1 = resolving, 2 = usable, 3 = permanently failed. */
static _Atomic int resolver_state;
static __thread int resolving;

/* ------------------------------------------------------------------ */
/* Thread-local reentrancy guard.                                      */
/*                                                                     */
/* dlsym(), vsnprintf() or any libc internals could themselves call    */
/* pthread_mutex_lock(). Without the guard, that re-enters this wrapper */
/* and recurses forever. __thread makes the flag per-thread: thread A   */
/* being inside the monitor never suppresses thread B's diagnostics.   */
/* ------------------------------------------------------------------ */

static __thread int in_monitor;

/* ------------------------------------------------------------------ */
/* Diagnostics: rdp_log() from src/log.c (declared in                  */
/* include/rdp/internal.h) - thread-safe, errno-preserving queued output,  */
/* safe to call from anywhere inside the monitor.                      */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* One-time resolution of the original functions.                      */
/*                                                                     */
/* dlsym(RTLD_NEXT, ...) must resolve the call that comes AFTER us in  */
/* the link order, i.e. glibc's implementation - never our own, which  */
/* would recurse. Resolved via pthread_once (race-free from any        */
/* thread) and ALSO from the constructor, so the common startup path   */
/* reports resolution failures immediately.                            */
/* ------------------------------------------------------------------ */

static void resolve_real_functions(void)
{
    const char *err;

    resolving = 1;
    atomic_store_explicit(&resolver_state, 1, memory_order_release);
    dlerror(); /* clear any stale error */
    real_lock = (real_lock_fn)dlsym(RTLD_NEXT, "pthread_mutex_lock");
    err = dlerror();
    if (err != NULL)
        real_lock = NULL;

    dlerror();
    real_unlock = (real_unlock_fn)dlsym(RTLD_NEXT, "pthread_mutex_unlock");
    err = dlerror();
    if (err != NULL)
        real_unlock = NULL;
    atomic_store_explicit(&resolver_state,
                          (real_lock != NULL && real_unlock != NULL) ? 2 : 3,
                          memory_order_release);
    resolving = 0;
}

/*
 * Practically unreachable: dlsym(RTLD_NEXT, ...) finds these symbols on
 * any glibc system. If it ever fails, report loudly and return a
 * nonzero pthread error code - never pretend the (un)lock succeeded.
 */
static int unresolved(const char *fn)
{
    /* This path must not enter the queued logger: it can be called while
     * pthread_once/dlsym are active. write is deliberately best effort. */
    static const char lock_message[] =
        "[deadlock_monitor] FATAL: pthread_mutex_lock could not be resolved; cannot forward the call\n";
    static const char unlock_message[] =
        "[deadlock_monitor] FATAL: pthread_mutex_unlock could not be resolved; cannot forward the call\n";
    const char *message = (fn[14] == 'l') ? lock_message : unlock_message;
    size_t length = (fn[14] == 'l') ? sizeof lock_message - 1 : sizeof unlock_message - 1;
    sigset_t set, old;
    sigemptyset(&set);
    sigaddset(&set, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &set, &old) == 0) {
        ssize_t written = write(STDERR_FILENO, message, length);
        (void)written;
        /* EPIPE may make SIGPIPE pending; consume it before restoring. */
        if (errno == EPIPE) {
            struct timespec zero = {0, 0};
            (void)sigtimedwait(&set, NULL, &zero);
        }
        (void)pthread_sigmask(SIG_SETMASK, &old, NULL);
    }
    errno = ENOSYS;
    return ENOSYS;
}

/* Graph API calls also use the existing guard, including direct queries. */
int rdp_monitor_enter(void)
{
    int previous = in_monitor;
    in_monitor = 1;
    if (resolving || atomic_load_explicit(&resolver_state, memory_order_acquire) == 3) {
        in_monitor = previous;
        return -1;
    }
    pthread_once(&resolver_once, resolve_real_functions);
    if (atomic_load_explicit(&resolver_state, memory_order_acquire) != 2) {
        in_monitor = previous;
        return -1;
    }
    return previous;
}
void rdp_monitor_leave(int previous) { in_monitor = previous; }
int rdp_monitor_available(void)
{
    return atomic_load_explicit(&resolver_state, memory_order_acquire) == 2;
}
/* Resolve early; normal operation intentionally emits no startup trace. */
__attribute__((constructor))
static void monitor_init(void)
{
    in_monitor = 1;
    rdp_graph_init();
    (void)rdp_monitor_enter();
    in_monitor = 0;
}

/* ------------------------------------------------------------------ */
/* Interposed functions.                                               */
/* ------------------------------------------------------------------ */

/* Analyze before acquiring; graph unlock precedes all warning output. */
static void analyze_dependencies(void *held[], size_t nheld, void *L)
{
    size_t ei;

    for (ei = 0; ei < nheld; ei++) {
        void *H = held[ei];
        void *cyc_path[RDP_GRAPH_MAX_CYC];
        size_t cyc_len = 0;
        void *ef = NULL, *et = NULL;
        int res = rdp_graph_cycle_for_edge(H, L, cyc_path, &cyc_len,
                                           &ef, &et);

        switch (res) {
        case RDP_GRAPH_CYCLE_FOUND:
            rdp_report_cycle((long)syscall(SYS_gettid), ef, et, cyc_path, cyc_len);
            break;
        case RDP_GRAPH_ANALYSIS_INCOMPLETE:
            rdp_log("[deadlock_monitor] graph: dependency %p -> %p NOT "
                    "recorded (capacity limit); analysis history "
                    "IS INCOMPLETE\n", H, L);
            break;
        case RDP_GRAPH_ALREADY_REPORTED:
            /* Same inverted order seen again: the first observation
             * already warned; repetition is a benign no-op. */
            break;
        case RDP_GRAPH_DUPLICATE:
            /* Edge already recorded. NOTE: a duplicate does NOT
             * re-trigger cycle analysis - the edge's cycle status was
             * already reported when the edge was first recorded, and
             * adding it again cannot create a NEW cycle. */
            break;
        case RDP_GRAPH_ADDED:
        case RDP_GRAPH_SELF_LOOP:
        default:
            break; /* no new cycle diagnostic */
        }
    }
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    void *held[RDP_TRACKER_CAPACITY];

    size_t nheld;
    int pre_errno = errno;
    int rc;

    if (in_monitor) {
        /* Recursive entry caused by monitor internals: forward straight
         * through - no logging, no bookkeeping - or the stack unwinds
         * forever (risk R2 in docs/RISKS.md). */
        return (rdp_monitor_available() && real_lock != NULL) ? real_lock(mutex)
                                   : unresolved("pthread_mutex_lock");
    }

    in_monitor = 1; /* guard ON: everything below is monitor code */

    /* Lazy resolution also covers the case where another library's
     * constructor calls pthread_mutex_lock before our constructor ran. */
    (void)rdp_monitor_enter();

    /* Phase 2, requirement 4: snapshot the held set BEFORE attempting
     * the lock. This is the exact input the Phase 3 graph module will
     * use to add edges held[i] -> mutex, and taking it before the
     * (possibly blocking) real call means the dependency is visible
     * even if the lock never returns. The pre_errno bracket guarantees
     * the real call still sees the application's original errno. */

    nheld = rdp_tracker_snapshot(held, RDP_TRACKER_CAPACITY);

    /* Phase 3: record dependencies + detect cycles BEFORE the real
     * (possibly blocking) call. The graph's internal mutex is taken
     * and released INSIDE rdp_graph_cycle_for_edge, so NO monitor
     * state is held across the real_pthread_mutex_lock below
     * (invariant I1: zero monitor state during the blocking call).
     */
    if (rdp_monitor_available() && real_lock != NULL)
        analyze_dependencies(held, nheld, (void *)mutex);

    if (rdp_monitor_available() && real_lock != NULL) {
        /* Invariant I1: no monitor lock is held here (the graph mutex
         * was released inside the graph module), so this blocking
         * call cannot deadlock against the monitor. */
        errno = pre_errno;
        rc = real_lock(mutex);
    } else {
        rc = unresolved("pthread_mutex_lock");
    }

    /* Requirements 3 and 6: only a SUCCESSFUL acquisition enters the
     * held set - a failed lock (e.g. EDEADLK on an error-checking
     * mutex) changes nothing. push() never touches errno. */
    if (rc == 0)
        rdp_tracker_push(mutex);

    in_monitor = 0; /* guard OFF - plain store, does not touch errno */
    return rc;      /* exact return value; errno as the real call left it */
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    int saved_errno = errno;
    int rc;

    if (in_monitor)
        return (rdp_monitor_available() && real_unlock != NULL) ? real_unlock(mutex)
                                     : unresolved("pthread_mutex_unlock");

    in_monitor = 1;
    pthread_once(&resolver_once, resolve_real_functions);

    errno = saved_errno;

    if (rdp_monitor_available() && real_unlock != NULL)
        rc = real_unlock(mutex);
    else
        rc = unresolved("pthread_mutex_unlock");

    /* Requirements 5 and 6: only a SUCCESSFUL release removes the
     * entry - a failed unlock (e.g. EPERM on an error-checking mutex)
     * leaves the held set untouched. pop() never touches errno. */
    if (rc == 0)
        rdp_tracker_pop(mutex);

    in_monitor = 0;
    return rc;
}





