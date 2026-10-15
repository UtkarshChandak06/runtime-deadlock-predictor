/*
 * include/rdp/tracker.h - per-thread held-lock tracker (Phase 2).
 *
 * Each thread gets its OWN collection of the mutexes it currently holds.
 * The collection lives in thread-local storage (__thread), so:
 *   - the hot path takes no locks (requirement 8),
 *   - one thread can never read or modify another thread's collection
 *     (requirement 11),
 *   - no cleanup hook is needed: TLS storage disappears with the thread.
 *
 * Mutex identity is the ADDRESS of the pthread_mutex_t (requirements 2
 * and 12): entries are compared as `void *` values, never dereferenced,
 * never formatted into names. Two different mutexes at the same address
 * at different times (destroy + re-init) are indistinguishable - a known
 * limitation documented in docs/ARCHITECTURE.md (mutex registry: Phase 4).
 *
 * Data structure: a fixed-capacity array of {mutex, depth} entries.
 * There is NO dynamic allocation anywhere in the tracking path, so an
 * allocation failure cannot occur (requirement 10 is satisfied by
 * construction). The corresponding completeness limit is capacity
 * overflow: when a thread holds more than RDP_TRACKER_CAPACITY distinct
 * mutexes at once, further locks are dropped, a warning is logged ONCE
 * per thread, and rdp_tracker_dropped() keeps counting - the monitor
 * reports that its view of this thread is incomplete rather than
 * silently lying.
 *
 * Recursive mutexes (requirement 12):
 *   - If the real lock SUCCEEDS on a mutex this thread already holds,
 *     that can only happen with a recursive (or error-checking re-lock
 *     that returned an error - then nothing happens) mutex, and we
 *     increment the entry's depth instead of adding a duplicate.
 *   - rdp_tracker_pop() decrements depth and removes the entry at 0,
 *     so lock x3 / unlock x3 ends with an empty collection.
 *   - The tracker never queries the mutex TYPE: POSIX has no portable
 *     "get attributes of an existing mutex" call, so behaviour is
 *     derived purely from the real function's return code. A re-lock of
 *     a NON-recursive mutex blocks forever inside the real call - we
 *     never add it a second time, but we also do not attempt to detect
 *     or warn about that self-deadlock (out of Phase 2 scope).
 *
 * Not yet tracked (until Phase 4 - do NOT assume coverage):
 *   - pthread_mutex_trylock / pthread_mutex_timedlock are not wrapped
 *     yet, so their successful acquisitions are invisible to us;
 *   - pthread_cond_wait/timedwait release and reacquire the mutex
 *     internally without passing through our wrapper, so the held set
 *     is stale while a thread waits on a condition variable;
 *   - robust/PI mutex owner-death paths are not handled;
 *   - init/destroy are not wrapped (address-reuse aliasing).
 */

#ifndef RDP_TRACKER_H
#define RDP_TRACKER_H

#include <stddef.h>

/* Maximum distinct mutexes one thread may hold while staying fully
 * tracked. Sized generously: real programs rarely nest more than a
 * handful of locks. Static (TLS) storage: 64 * sizeof(entry) ~ 1 KiB
 * per thread. */
#define RDP_TRACKER_CAPACITY 64

/*
 * Copy the calling thread's held mutexes into held[] (at most `max`
 * entries) and return how many were copied. This is the read API the
 * lock-order graph module will consume in Phase 3; the wrapper calls it
 * BEFORE attempting a new lock so lock-order intent (held[i] -> new)
 * can be recorded even if the lock blocks forever.
 * Pure pointer copies: no locks, no allocation, errno untouched.
 */
size_t rdp_tracker_snapshot(void *held[], size_t max);

/*
 * Record that the calling thread now holds `mutex`. Called ONLY after
 * the real lock returned success (failed operations are never tracked).
 * Depth++ if this thread already holds it; otherwise appends an entry,
 * or reports an overflow (once per thread) if the set is full.
 */
void rdp_tracker_push(void *mutex);

/*
 * Record that the calling thread released `mutex`. Returns the
 * remaining depth (0 = entry removed) or -1 if the calling thread did
 * not hold it. Called ONLY after the real unlock returned success.
 */
int rdp_tracker_pop(void *mutex);

/* Does the calling thread currently hold `mutex`? 1 / 0. */
int rdp_tracker_holds(void *mutex);

/* Current recursion depth of `mutex` for this thread (0 = not held). */
size_t rdp_tracker_depth(void *mutex);

/* Number of DISTINCT mutexes the calling thread currently holds. */
size_t rdp_tracker_count(void);

/* Locks this thread made that were DROPPED because the set was full
 * (0 = tracking complete). Per-thread. */
size_t rdp_tracker_dropped(void);

#endif /* RDP_TRACKER_H */
