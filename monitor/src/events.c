#define _GNU_SOURCE
#include "rdp/events.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t output_mutex = PTHREAD_MUTEX_INITIALIZER;
static int event_fd = -1;
static uint64_t sequence;
static bool healthy = true;
typedef struct { char data[4096]; size_t used; bool valid; } Buffer;

static void append(Buffer *b, const char *format, ...)
{
    if (!b->valid) return;
    va_list args; va_start(args, format);
    int n = vsnprintf(b->data + b->used, sizeof(b->data) - b->used, format, args);
    va_end(args);
    if (n < 0 || (size_t)n >= sizeof(b->data) - b->used) b->valid = false;
    else b->used += (size_t)n;
}
/* Full JSON string escaping, even though all current labels are internal. */
static void string(Buffer *b, const char *s)
{
    append(b, "\"");
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        if (*p == '"' || *p == '\\') append(b, "\\%c", *p);
        else if (*p < 32 || *p >= 127) append(b, "\\u%04x", (unsigned)*p);
        else append(b, "%c", *p);
    }
    append(b, "\"");
}
static void id(Buffer *b, RdpLockId lock) { append(b, "\"lock_%" PRIx64 "\"", lock); }

/* Protect only this thread from SIGPIPE; do not change the application's signal
 * disposition. Consume only a newly generated pending SIGPIPE after EPIPE. */
static ssize_t safe_write(int fd, const void *data, size_t size)
{
    sigset_t set, previous, pending;
    sigemptyset(&set); sigaddset(&set, SIGPIPE);
    int status = pthread_sigmask(SIG_BLOCK, &set, &previous);
    if (status != 0) { errno = status; return -1; }
    if (sigpending(&pending) != 0) {
        int saved = errno; (void)pthread_sigmask(SIG_SETMASK, &previous, NULL);
        errno = saved; return -1;
    }
    bool already_pending = sigismember(&pending, SIGPIPE) == 1;
    ssize_t written;
    do { written = syscall(SYS_write, fd, data, size); } while (written < 0 && errno == EINTR);
    int saved = errno;
    if (written < 0 && saved == EPIPE && !already_pending) {
        const struct timespec zero = {0, 0};
        while (sigtimedwait(&set, NULL, &zero) < 0 && errno == EINTR) { }
    }
    (void)pthread_sigmask(SIG_SETMASK, &previous, NULL);
    errno = saved;
    return written;
}
bool rdp_events_init(void)
{
    const char *value = getenv("RDP_EVENT_FD");
    int source = STDERR_FILENO;
    bool valid = true;
    if (value) {
        unsigned long number = 0;
        if (!*value) valid = false;
        for (const unsigned char *p = (const unsigned char *)value; valid && *p; ++p) {
            if (*p < '0' || *p > '9' || number > ((unsigned long)INT_MAX - (*p - '0')) / 10)
                valid = false;
            else number = number * 10 + (*p - '0');
        }
        if (valid) {
            source = (int)number;
            int flags = fcntl(source, F_GETFL);
            if (source < 2 || flags < 0 || (flags & O_ACCMODE) == O_RDONLY) valid = false;
        }
    }
    if (!valid) source = STDERR_FILENO;
    event_fd = fcntl(source, F_DUPFD_CLOEXEC, 3);
    if (event_fd < 0) { event_fd = STDERR_FILENO; valid = false; }
    return valid;
}
bool rdp_events_healthy(void)
{
    if (pthread_mutex_lock(&output_mutex) != 0) return false;
    bool result = healthy;
    if (pthread_mutex_unlock(&output_mutex) != 0) return false;
    return result;
}
bool rdp_event_emit(const RdpEvent *event)
{
    if (pthread_mutex_lock(&output_mutex) != 0) return false;
    if (!healthy) { (void)pthread_mutex_unlock(&output_mutex); return false; }
    Buffer b = {.valid = true}; struct timespec now = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 || sequence == UINT64_MAX) b.valid = false;
    if (b.valid) {
        append(&b, "{\"schema_version\":1,\"session_event\":"); string(&b, event->type);
        append(&b, ",\"timestamp_ns\":%" PRIu64 ",\"process_id\":%ld,\"thread_id\":%ld,"
               "\"thread_instance\":%" PRIu64 ",\"sequence\":%" PRIu64 ",\"monitoring_complete\":%s",
               (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec,
               (long)getpid(), (long)syscall(SYS_gettid), event->thread_instance,
               ++sequence, event->complete ? "true" : "false");
        if (event->lock) { append(&b, ",\"lock_id\":"); id(&b, event->lock); }
        if (event->address) append(&b, ",\"mutex_address\":\"0x%" PRIxPTR "\"", event->address);
        if (event->kind) { append(&b, ",\"lock_kind\":"); string(&b, event->kind); }
        if (event->has_result) append(&b, ",\"return_code\":%d", event->return_code);
        else if (event->return_code) append(&b, ",\"monitor_status\":%d", event->return_code);
        if (event->reason) { append(&b, ",\"reason\":"); string(&b, event->reason); }
        if (event->snapshot) {
            const RdpSnapshot *s = event->snapshot;
            append(&b, ",\"held_locks\":[");
            for (size_t i = 0; i < s->count; ++i) { if (i) append(&b, ","); id(&b, s->held[i].lock); }
            append(&b, "],\"held_depths\":[");
            for (size_t i = 0; i < s->count; ++i) append(&b, "%s%" PRIu32, i ? "," : "", s->held[i].depth);
            append(&b, "]");
        }
        if (event->warning) {
            const RdpWarning *w = event->warning;
            append(&b, ",\"held_lock\":"); id(&b, w->held_lock);
            append(&b, ",\"requested_lock\":"); id(&b, w->requested_lock);
            append(&b, ",\"closing_edge\":["); id(&b, w->held_lock); append(&b, ","); id(&b, w->requested_lock);
            append(&b, "],\"graph_sequence\":%" PRIu64 ",\"cycle\":[", w->sequence);
            for (size_t i = 0; i < w->cycle_length; ++i) { if (i) append(&b, ","); id(&b, w->cycle[i]); }
            append(&b, "]");
        }
        append(&b, "}\n");
    }
    /* <= Linux PIPE_BUF: one write keeps records atomic to a pipe. A partial
     * write/error disables this sink rather than emitting a malformed suffix. */
    if (!b.valid || safe_write(event_fd, b.data, b.used) != (ssize_t)b.used) {
        healthy = false;
        Buffer fallback = {.valid = true};
        append(&fallback, "{\"schema_version\":1,\"session_event\":\"sink_error\","
               "\"timestamp_ns\":%" PRIu64 ",\"process_id\":%ld,\"thread_id\":%ld,"
               "\"thread_instance\":%" PRIu64 ",\"sequence\":%" PRIu64 ","
               "\"reason\":\"event_delivery_failed\",\"monitoring_complete\":false}\n",
               (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec,
               (long)getpid(), (long)syscall(SYS_gettid), event->thread_instance, sequence);
        if (fallback.valid) (void)safe_write(STDERR_FILENO, fallback.data, fallback.used);
    }
    bool result = healthy;
    if (pthread_mutex_unlock(&output_mutex) != 0) return false;
    return result;
}
