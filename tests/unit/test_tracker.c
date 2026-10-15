/*
 * tests/unit/test_tracker.c - unit tests for src/tracker.c.
 *
 * Links tracker.c + log.c DIRECTLY (no LD_PRELOAD, no wrappers): the
 * tracker is tested as a pure data structure, addressing mutexes by
 * arbitrary address tokens (the tracker never dereferences them).
 *
 * Covers (Phase 2 requirements):
 *   - empty collection initially and after every release (req 5, test 5),
 *   - add on push / remove on pop (req 3, 5),
 *   - depth counting for successful re-locks (req 12, recursive mutex),
 *   - pop of a non-held mutex reports failure, state unchanged (req 6),
 *   - NULL handling, snapshot bounds (req 4 read API),
 *   - capacity overflow is REPORTED, not silent; recovery after pop
 *     (req 10 - fixed array means no allocation can fail at all),
 *   - one thread can never see or touch another thread's collection
 *     (req 11).
 */

#include "rdp/tracker.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

/* Address tokens: distinct real objects, never dereferenced. */
static char slot_a, slot_b, slot_c;
#define A ((void *)&slot_a)
#define B ((void *)&slot_b)
#define C ((void *)&slot_c)

#define CHECK(cond, what)                                                \
    do {                                                                 \
        if (!(cond)) {                                                   \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                    (what));                                             \
            exit(EXIT_FAILURE);                                          \
        }                                                                \
    } while (0)

static void ok(const char *name)
{
    printf("ok - %s\n", name);
}

/* ---- 1. empty initially and after release ---- */
static void test_empty_state(void)
{
    void *buf[4];

    CHECK(rdp_tracker_count() == 0, "starts empty");
    CHECK(rdp_tracker_holds(A) == 0, "does not hold A initially");
    CHECK(rdp_tracker_depth(A) == 0, "depth of A is 0 initially");
    CHECK(rdp_tracker_dropped() == 0, "nothing dropped initially");
    CHECK(rdp_tracker_snapshot(buf, 4) == 0, "snapshot of empty set is 0");

    rdp_tracker_push(A);
    CHECK(rdp_tracker_count() == 1, "count 1 after push");
    CHECK(rdp_tracker_pop(A) == 0, "pop returns remaining depth 0");
    CHECK(rdp_tracker_count() == 0, "EMPTY after all locks released");
    CHECK(rdp_tracker_holds(A) == 0, "A no longer held");
    ok("empty initially and after release");
}

/* ---- 2. push/pop with multiple mutexes ---- */
static void test_push_pop(void)
{
    void *buf[4];

    rdp_tracker_push(A);
    rdp_tracker_push(B);
    CHECK(rdp_tracker_count() == 2, "count 2 after two pushes");
    CHECK(rdp_tracker_holds(A) && rdp_tracker_holds(B), "holds A and B");
    CHECK(rdp_tracker_holds(C) == 0, "does not hold C");

    CHECK(rdp_tracker_snapshot(buf, 4) == 2, "snapshot returns 2");
    CHECK((buf[0] == A && buf[1] == B) || (buf[0] == B && buf[1] == A),
          "snapshot contains A and B");

    CHECK(rdp_tracker_pop(B) == 0, "pop B -> depth 0");
    CHECK(rdp_tracker_count() == 1, "count 1 after popping B");
    CHECK(rdp_tracker_holds(A) && !rdp_tracker_holds(B), "only A left");

    CHECK(rdp_tracker_pop(A) == 0, "pop A -> depth 0");
    CHECK(rdp_tracker_count() == 0, "empty again");
    ok("push/pop of multiple mutexes");
}

/* ---- 3. depth counting (recursive re-lock semantics) ---- */
static void test_depth_counting(void)
{
    rdp_tracker_push(A);
    rdp_tracker_push(A); /* what push() does after a successful re-lock */
    rdp_tracker_push(A);
    CHECK(rdp_tracker_count() == 1, "still ONE distinct entry");
    CHECK(rdp_tracker_depth(A) == 3, "depth 3 after three pushes");
    CHECK(rdp_tracker_holds(A) == 1, "holds A");

    CHECK(rdp_tracker_pop(A) == 2, "pop -> depth 2");
    CHECK(rdp_tracker_count() == 1, "still held at depth 2");
    CHECK(rdp_tracker_pop(A) == 1, "pop -> depth 1");
    CHECK(rdp_tracker_pop(A) == 0, "pop -> depth 0, entry removed");
    CHECK(rdp_tracker_count() == 0, "empty after balanced re-locks");
    ok("depth counting for recursive re-locks");
}

/* ---- 4. failed operations never change state ---- */
static void test_failed_ops(void)
{
    rdp_tracker_push(A);
    CHECK(rdp_tracker_pop(B) == -1, "pop of non-held mutex returns -1");
    CHECK(rdp_tracker_count() == 1, "state unchanged after failed pop");
    CHECK(rdp_tracker_pop(NULL) == -1, "pop(NULL) returns -1");
    rdp_tracker_push(NULL);
    CHECK(rdp_tracker_count() == 1, "push(NULL) is ignored");
    CHECK(rdp_tracker_pop(A) == 0, "cleanup pop");
    CHECK(rdp_tracker_count() == 0, "empty");
    ok("failed operations leave state unchanged");
}

/* ---- 5. snapshot respects the caller's buffer ---- */
static void test_snapshot_bounds(void)
{
    void *buf[4];

    rdp_tracker_push(A);
    rdp_tracker_push(B);
    CHECK(rdp_tracker_snapshot(buf, 0) == 0, "max=0 copies nothing");
    CHECK(rdp_tracker_snapshot(buf, 1) == 1, "max=1 copies one");
    CHECK(rdp_tracker_snapshot(buf, 4) == 2, "max=4 copies both");
    rdp_tracker_pop(A);
    rdp_tracker_pop(B);
    CHECK(rdp_tracker_count() == 0, "empty");
    ok("snapshot honours buffer capacity");
}

/* ---- 6. capacity overflow is reported, then recoverable ---- */
static void test_capacity_overflow(void)
{
    static char slots[RDP_TRACKER_CAPACITY + 4];
    void *buf[RDP_TRACKER_CAPACITY];

    /* Fill the set to capacity. */
    for (int i = 0; i < RDP_TRACKER_CAPACITY; i++)
        rdp_tracker_push((void *)&slots[i]);
    CHECK(rdp_tracker_count() == RDP_TRACKER_CAPACITY, "set is full");
    CHECK(rdp_tracker_dropped() == 0, "nothing dropped while fitting");

    /* One over capacity: must be DROPPED and REPORTED (a warning line
     * is logged to stderr), not silently accepted. */
    rdp_tracker_push((void *)&slots[RDP_TRACKER_CAPACITY]);
    CHECK(rdp_tracker_dropped() == 1, "overflow counted as dropped");
    CHECK(rdp_tracker_count() == RDP_TRACKER_CAPACITY, "capacity intact");

    /* Re-locking an entry that IS tracked still works while full. */
    rdp_tracker_push((void *)&slots[0]);
    CHECK(rdp_tracker_depth((void *)&slots[0]) == 2, "depth works when full");
    CHECK(rdp_tracker_dropped() == 1, "re-lock does not count as drop");
    rdp_tracker_pop((void *)&slots[0]);

    /* Make room: tracking resumes for new mutexes. */
    rdp_tracker_pop((void *)&slots[1]);
    rdp_tracker_push((void *)&slots[RDP_TRACKER_CAPACITY + 1]);
    CHECK(rdp_tracker_count() == RDP_TRACKER_CAPACITY, "refilled");
    CHECK(rdp_tracker_dropped() == 1, "no new drops after making room");

    CHECK(rdp_tracker_snapshot(buf, RDP_TRACKER_CAPACITY) ==
              RDP_TRACKER_CAPACITY,
          "snapshot covers the full set");

    /* Drain to empty again. */
    for (int i = 2; i < RDP_TRACKER_CAPACITY + 2; i++)
        rdp_tracker_pop((void *)&slots[i]);
    rdp_tracker_pop((void *)&slots[0]);
    CHECK(rdp_tracker_count() == 0, "empty after draining");
    ok("capacity overflow reported and recoverable");
}

/* ---- 7. threads cannot see each other's collections ---- */
static char slot_main, slot_worker_b, slot_worker_c;
#define MAIN_M ((void *)&slot_main)
#define WORKER_B ((void *)&slot_worker_b)
#define WORKER_C ((void *)&slot_worker_c)

static void *worker(void *arg)
{
    (void)arg;

    /* Fresh thread starts empty and cannot see MAIN's entry (req 11). */
    CHECK(rdp_tracker_count() == 0, "worker starts empty");
    CHECK(rdp_tracker_holds(MAIN_M) == 0, "worker cannot see main's lock");

    rdp_tracker_push(WORKER_B);
    rdp_tracker_push(WORKER_B);
    CHECK(rdp_tracker_count() == 1, "worker tracks its own 1 entry");
    CHECK(rdp_tracker_depth(WORKER_B) == 2, "worker depth 2");
    CHECK(rdp_tracker_holds(MAIN_M) == 0, "still cannot see main's lock");

    CHECK(rdp_tracker_pop(WORKER_B) == 1, "worker pop -> 1");
    CHECK(rdp_tracker_pop(WORKER_B) == 0, "worker pop -> 0");

    /* Leave C held on purpose: TLS disappears with the thread, and
     * main must never observe it. */
    rdp_tracker_push(WORKER_C);
    return NULL;
}

static void test_thread_isolation(void)
{
    pthread_t th;

    rdp_tracker_push(MAIN_M);
    CHECK(rdp_tracker_count() == 1, "main holds its lock");

    CHECK(pthread_create(&th, NULL, worker, NULL) == 0,
          "pthread_create");
    CHECK(pthread_join(th, NULL) == 0, "pthread_join");

    /* Main's collection is untouched by the worker's pushes/pops. */
    CHECK(rdp_tracker_count() == 1, "main still has exactly its entry");
    CHECK(rdp_tracker_holds(MAIN_M) == 1, "main still holds its lock");
    CHECK(rdp_tracker_holds(WORKER_C) == 0, "main cannot see worker's C");
    CHECK(rdp_tracker_holds(WORKER_B) == 0, "main cannot see worker's B");

    rdp_tracker_pop(MAIN_M);
    CHECK(rdp_tracker_count() == 0, "main empty after release");
    ok("per-thread isolation");
}

int main(void)
{
    test_empty_state();
    test_push_pop();
    test_depth_counting();
    test_failed_ops();
    test_snapshot_bounds();
    test_capacity_overflow();
    test_thread_isolation();

    printf("TEST test_tracker PASS\n");
    return EXIT_SUCCESS;
}
