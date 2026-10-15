/*
 * src/tracker.c - per-thread held-lock collection (Phase 2).
 *
 * Design: one fixed-size thread-local array of {mutex, depth} entries
 * per thread. Rationale (see include/rdp/tracker.h for the full notes):
 *
 * - __thread storage  -> thread isolation for free (requirement 11),
 *                        no locks on the hot path (requirement 8),
 *                        no destructor/cleanup needed;
 * - fixed capacity    -> zero dynamic allocation, so allocation failure
 *                        cannot happen; overflow is REPORTED (requirement
 *                        10) instead of silently truncating;
 * - address identity  -> entries are opaque `void *` tokens, never
 *                        dereferenced (requirement 2);
 * - depth counting    -> a successful re-lock by the same thread (only
 *                        possible with a recursive mutex) increments the
 *                        depth instead of creating a duplicate entry
 *                        (requirement 12).
 *
 * All functions operate on the CALLING thread's storage - there is no
 * way to reach another thread's collection, which is exactly the
 * isolation property requirement 11 asks for.
 */

#include "rdp/tracker.h"
#include "rdp/internal.h"

typedef struct {
    void  *mutex; /* pthread_mutex_t * used as an opaque address token */
    size_t depth; /* >= 1 while held; entry lives while depth > 0      */
} held_entry;

/* Thread-local collection. Zero-initialized: a new thread starts empty,
 * so no explicit init function is needed. */
static __thread held_entry rdp_held[RDP_TRACKER_CAPACITY];
static __thread size_t rdp_held_count;
static __thread size_t rdp_held_dropped;
static __thread int rdp_held_overflow_logged;

/* Linear search: capacity is tiny (64) and threads hold few locks, so
 * an unsorted array beats a hash table here - and stays simple enough
 * to explain in a course project. */
static held_entry *find_entry(void *mutex)
{
    for (size_t i = 0; i < rdp_held_count; i++) {
        if (rdp_held[i].mutex == mutex)
            return &rdp_held[i];
    }
    return NULL;
}

void rdp_tracker_push(void *mutex)
{
    held_entry *e;

    if (mutex == NULL)
        return; /* defensive: never track a NULL "lock" */

    e = find_entry(mutex);
    if (e != NULL) {
        /* Already held by THIS thread: only a successful re-lock gets
         * here (a recursive mutex). Count depth, do not duplicate. */
        e->depth++;
        return;
    }

    if (rdp_held_count >= RDP_TRACKER_CAPACITY) {
        /* Requirement 10: report incompleteness instead of pretending.
         * Warn once per thread; keep counting drops the whole run. */
        rdp_held_dropped++;
        if (!rdp_held_overflow_logged) {
            rdp_held_overflow_logged = 1;
            rdp_log("[deadlock_monitor] tracker: held-lock set full (%d); "
                    "further locks for this thread are NOT tracked - "
                    "monitoring INCOMPLETE\n",
                    RDP_TRACKER_CAPACITY);
        }
        return;
    }

    rdp_held[rdp_held_count].mutex = mutex;
    rdp_held[rdp_held_count].depth = 1;
    rdp_held_count++;
}

int rdp_tracker_pop(void *mutex)
{
    held_entry *e;
    size_t remaining;

    if (mutex == NULL)
        return -1;

    e = find_entry(mutex);
    if (e == NULL)
        return -1; /* not held by this thread: nothing to remove */

    remaining = e->depth - 1;
    if (remaining == 0) {
        /* Swap-remove keeps the array compact; order is irrelevant. */
        rdp_held_count--;
        *e = rdp_held[rdp_held_count];
    } else {
        e->depth = remaining; /* still held (recursive nesting) */
    }
    return (int)remaining;
}

int rdp_tracker_holds(void *mutex)
{
    return mutex != NULL && find_entry(mutex) != NULL;
}

size_t rdp_tracker_depth(void *mutex)
{
    held_entry *e = (mutex != NULL) ? find_entry(mutex) : NULL;
    return (e != NULL) ? e->depth : 0;
}

size_t rdp_tracker_count(void)
{
    return rdp_held_count;
}

size_t rdp_tracker_dropped(void)
{
    return rdp_held_dropped;
}

size_t rdp_tracker_snapshot(void *held[], size_t max)
{
    size_t n = rdp_held_count;

    if (n > max)
        n = max; /* never write past the caller's buffer */
    for (size_t i = 0; i < n; i++)
        held[i] = rdp_held[i].mutex;
    return n;
}
