#include "rdp/internal.h"
#include "rdp/report.h"
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define QUEUE_CAP 16
static struct { size_t len; char data[RDP_LOG_RECORD_CAP]; } queue[QUEUE_CAP];
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static size_t head, count, lost, errors, pending_lost;
static int draining;
static __thread int inside_log;

static void increment(size_t *n) { if (*n != SIZE_MAX) ++*n; }
/* SIGPIPE is blocked only on this thread. Consume only a signal generated
 * by our failed write, preserving a previously pending application signal. */
static int emit(const char *data, size_t len)
{
    sigset_t set, old, pending;
    int had_pipe, failed = 0;
    sigemptyset(&set);
    sigaddset(&set, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &set, &old)) return -1;
    sigpending(&pending);
    had_pipe = sigismember(&pending, SIGPIPE);
    while (len) {
        ssize_t n = write(STDERR_FILENO, data, len);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            if (n < 0 && errno == EPIPE && !had_pipe) {
                struct timespec zero = {0, 0};
                (void)sigtimedwait(&set, NULL, &zero);
            }
            failed = -1;
            break;
        }
        data += n;
        len -= (size_t)n;
    }
    pthread_sigmask(SIG_SETMASK, &old, NULL);
    return failed;
}
/* Cancellation of the elected writer must not strand queue ownership.
 * Cancellation is disabled for short queue critical sections only. */
static void abandon(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&mutex);
    draining = 0;
    increment(&errors);
    pthread_mutex_unlock(&mutex);
}
static void drain(int original_cancel_state)
{
    char local[RDP_LOG_RECORD_CAP];
    int previous;
    pthread_cleanup_push(abandon, NULL);
    for (;;) {
        size_t len;
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previous);
        pthread_mutex_lock(&mutex);
        if (pending_lost) {
            /* Format outside the lock just like ordinary records. */
            size_t n = pending_lost;
            pending_lost = 0;
            pthread_mutex_unlock(&mutex);
            len = (size_t)snprintf(local, sizeof local,
                "[deadlock_monitor] diagnostics INCOMPLETE: %zu records dropped (queue capacity %d)\n", n, QUEUE_CAP);
        } else if (count) {
            len = queue[head].len;
            memcpy(local, queue[head].data, len);
            head = (head + 1) % QUEUE_CAP;
            --count;
            pthread_mutex_unlock(&mutex);
        } else {
            draining = 0;
            pthread_mutex_unlock(&mutex);
            /* Keep cancellation disabled until cleanup has been popped. */
            break;
        }
        pthread_setcancelstate(original_cancel_state, NULL);
        int failed = emit(local, len);
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previous);
        pthread_mutex_lock(&mutex);
        if (failed) increment(&errors);
        pthread_mutex_unlock(&mutex);
        pthread_setcancelstate(previous, NULL);
    }
    pthread_cleanup_pop(0);
}
void rdp_log_record(const char *record, size_t len)
{
    int saved = errno, guard = 0, previous, elected = 0;
    if (inside_log) return;
    inside_log = 1;
#ifdef RDP_INTERPOSE
    guard = rdp_monitor_enter();
    if (guard == -1) {
        inside_log = 0;
        errno = saved;
        return;
    }
#endif
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previous);
    pthread_mutex_lock(&mutex);
    if (!record || len > RDP_LOG_RECORD_CAP || count == QUEUE_CAP) {
        increment(&lost);
        increment(&pending_lost);
    } else {
        size_t slot = (head + count) % QUEUE_CAP;
        memcpy(queue[slot].data, record, len);
        queue[slot].len = len;
        ++count;
    }
    if (!draining) { draining = 1; elected = 1; }
    pthread_mutex_unlock(&mutex);
    /* An elected writer installs cancellation cleanup before reenabling. */
    if (elected) {
        /* drain owns cleanup; entry remains cancellation-disabled. */
        drain(previous);
    }
    pthread_setcancelstate(previous, NULL);
#ifdef RDP_INTERPOSE
    rdp_monitor_leave(guard);
#else
    (void)guard;
#endif
    inside_log = 0;
    errno = saved;
}
void rdp_log(const char *fmt, ...)
{
    int saved = errno;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n >= 0 && (size_t)n < sizeof buf) rdp_log_record(buf, (size_t)n);
    else {
        static const char failed[] = "[deadlock_monitor] diagnostic formatting overflow; diagnostics INCOMPLETE\n";
        rdp_log_record(failed, sizeof failed - 1);
    }
    errno = saved;
}
static size_t counter(int failures)
{
    int saved = errno, guard = 0;
#ifdef RDP_INTERPOSE
    guard = rdp_monitor_enter();
    if (guard == -1) {
        errno = saved;
        return SIZE_MAX;
    }
#endif
    pthread_mutex_lock(&mutex);
    size_t n = failures ? errors : lost;
    pthread_mutex_unlock(&mutex);
#ifdef RDP_INTERPOSE
    rdp_monitor_leave(guard);
#else
    (void)guard;
#endif
    errno = saved;
    return n;
}
size_t rdp_log_dropped(void) { return counter(0); }
size_t rdp_log_errors(void) { return counter(1); }

