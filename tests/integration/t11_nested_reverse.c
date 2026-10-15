/*
 * tests/integration/t11_nested_reverse.c - Phase 2 integration test.
 *
 * One thread acquires TWO mutexes in nesting order (m1 then m2) and
 * releases them in REVERSE order (m2 then m1) - the classic safe
 * pattern that must never warn or deadlock.
 *
 * Checks:
 *   - count is 1 -> 2 -> 1 -> 0 through the sequence (req 3, 5),
 *   - snapshot-style visibility: both mutexes tracked while nested,
 *     neither after the final release (state returns to empty),
 *   - all pthread return codes are 0.
 */

#include "tracker_probe.h"

#include <pthread.h>

int main(void)
{
    tracker_probe probe;
    pthread_mutex_t m1 = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t m2 = PTHREAD_MUTEX_INITIALIZER;
    int rc;

    tracker_probe_load(&probe);
    if (probe.available)
        TEST_CHECK(probe.count() == 0, "tracker empty at start");

    rc = pthread_mutex_lock(&m1);
    TEST_CHECK(rc == 0, "lock m1 rc == 0");
    if (probe.available) {
        TEST_CHECK(probe.count() == 1, "count 1 after first lock");
        TEST_CHECK(probe.holds(&m1) == 1, "holds m1");
        TEST_CHECK(probe.holds(&m2) == 0, "does not hold m2 yet");
    }

    rc = pthread_mutex_lock(&m2);
    TEST_CHECK(rc == 0, "lock m2 rc == 0");
    if (probe.available) {
        TEST_CHECK(probe.count() == 2, "count 2 while nested");
        TEST_CHECK(probe.holds(&m1) && probe.holds(&m2),
                   "holds both while nested");
        TEST_CHECK(probe.depth(&m1) == 1 && probe.depth(&m2) == 1,
                   "both at depth 1 (no duplicates)");
    }

    rc = pthread_mutex_unlock(&m2);
    TEST_CHECK(rc == 0, "unlock m2 rc == 0");
    if (probe.available) {
        TEST_CHECK(probe.count() == 1, "count 1 after releasing m2");
        TEST_CHECK(probe.holds(&m2) == 0, "m2 removed");
        TEST_CHECK(probe.holds(&m1) == 1, "m1 still held");
    }

    rc = pthread_mutex_unlock(&m1);
    TEST_CHECK(rc == 0, "unlock m1 rc == 0");
    if (probe.available)
        TEST_CHECK(probe.count() == 0, "tracker EMPTY after reverse release");

    printf("TEST t11 PASS\n");
    return EXIT_SUCCESS;
}
