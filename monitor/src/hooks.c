#define _GNU_SOURCE
#include "rdp/runtime.h"
#include <dlfcn.h>
#include <errno.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

static int (*real_lock)(pthread_mutex_t *);
static int (*real_unlock)(pthread_mutex_t *);
static int (*real_init)(pthread_mutex_t *, const pthread_mutexattr_t *);
static int (*real_destroy)(pthread_mutex_t *);
static pthread_once_t symbols_once = PTHREAD_ONCE_INIT;
static _Thread_local bool resolving;

static void resolution_failure(void)
{
    const char message[] = "Deadlock Sentinel: original pthread symbol unavailable; cannot safely forward operation\n";
    (void)syscall(SYS_write, STDERR_FILENO, message, sizeof(message) - 1);
    _exit(127);
}
static void load_symbol(void *destination, size_t size, const char *name)
{
    (void)dlerror();
    void *symbol = dlsym(RTLD_NEXT, name);
    if (dlerror() || !symbol || size != sizeof(symbol)) resolution_failure();
    /* POSIX dlsym supports function addresses; memcpy avoids ISO C cast warnings. */
    memcpy(destination, &symbol, size);
}
static void resolve_symbols(void)
{
    resolving = true; ++rdp_runtime_guard;
    load_symbol(&real_lock, sizeof(real_lock), "pthread_mutex_lock");
    load_symbol(&real_unlock, sizeof(real_unlock), "pthread_mutex_unlock");
    load_symbol(&real_init, sizeof(real_init), "pthread_mutex_init");
    load_symbol(&real_destroy, sizeof(real_destroy), "pthread_mutex_destroy");
    --rdp_runtime_guard; resolving = false;
}
static void resolve(void)
{
    if (!resolving && pthread_once(&symbols_once, resolve_symbols) != 0) resolution_failure();
}
/* Mutex calls are not deferred-cancellation points. Disable cancellation around
 * monitor internals and the real operation so logging introduces no new point.
 * Asynchronous cancellation is outside the supported scope. */
static int begin(void)
{
    int previous;
    if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previous) != 0) resolution_failure();
    ++rdp_runtime_guard;
    return previous;
}
static void finish(int previous, int saved_errno)
{
    --rdp_runtime_guard;
    (void)pthread_setcancelstate(previous, NULL);
    errno = saved_errno;
}
int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    int entry_errno = errno; resolve();
    if (!real_lock) resolution_failure();
    if (rdp_runtime_guard || resolving) { errno = entry_errno; return real_lock(mutex); }
    int previous = begin(); bool active = rdp_runtime_ready();
    RdpRuntimeCall call = {0};
    if (active) rdp_runtime_before_lock(mutex, &call);
    --rdp_runtime_guard; errno = entry_errno;
    int result = real_lock(mutex); int result_errno = errno;
    ++rdp_runtime_guard;
    if (active) rdp_runtime_after_lock(&call, result);
    finish(previous, result_errno); return result;
}
int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    int entry_errno = errno; resolve();
    if (!real_unlock) resolution_failure();
    if (rdp_runtime_guard || resolving) { errno = entry_errno; return real_unlock(mutex); }
    int previous = begin(); bool active = rdp_runtime_ready();
    RdpRuntimeCall call = {0};
    if (active) rdp_runtime_before_unlock(mutex, &call);
    --rdp_runtime_guard; errno = entry_errno;
    int result = real_unlock(mutex); int result_errno = errno;
    ++rdp_runtime_guard;
    if (active) rdp_runtime_after_unlock(&call, result);
    finish(previous, result_errno); return result;
}
int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    int entry_errno = errno; resolve();
    if (!real_init) resolution_failure();
    if (rdp_runtime_guard || resolving) { errno = entry_errno; return real_init(mutex, attr); }
    int previous = begin(); bool active = rdp_runtime_ready();
    --rdp_runtime_guard; errno = entry_errno;
    int result = real_init(mutex, attr); int result_errno = errno;
    ++rdp_runtime_guard;
    if (active) rdp_runtime_after_init(mutex, attr, result);
    finish(previous, result_errno); return result;
}
int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    int entry_errno = errno; resolve();
    if (!real_destroy) resolution_failure();
    if (rdp_runtime_guard || resolving) { errno = entry_errno; return real_destroy(mutex); }
    int previous = begin(); bool active = rdp_runtime_ready();
    --rdp_runtime_guard; errno = entry_errno;
    int result = real_destroy(mutex); int result_errno = errno;
    ++rdp_runtime_guard;
    if (active) rdp_runtime_after_destroy(mutex, result);
    finish(previous, result_errno); return result;
}
__attribute__((constructor)) static void monitor_start(void)
{
    int saved_errno = errno; resolve(); int previous = begin();
    (void)rdp_runtime_ready(); finish(previous, saved_errno);
}
__attribute__((destructor)) static void monitor_end(void)
{
    int saved_errno = errno; int previous = begin();
    rdp_runtime_finish(); finish(previous, saved_errno);
}
