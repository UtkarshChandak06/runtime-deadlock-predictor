/*
 * tests/integration/t10_single_mutex.c - Phase 2 integration test.
 *
 * A single thread acquires and releases ONE mutex (twice), checking:
 *   - pthread return codes are 0 and errno is left untouched,
 *   - the tracker holds exactly that mutex while held (req 3),
 *   - the tracker is EMPTY again after the release (req 5, the
 *     "state returns to empty" scenario),
 *   - a second acquire/release cycle starts from empty state.
 *
 * Runs the same binary both ways:
 *   bare      -> pthread behaviour only (transparency),
 *   preloaded -> additionally asserts tracker state via dlsym probe.
 */

#include "tracker_probe.h"

#include <errno.h>
#include <pthread.h>

int main(void)
{
    tracker_probe probe;
    pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    int rc;

    tracker_probe_load(&probe);
    if (probe.available)
        TEST_CHECK(probe.count() == 0, "tracker empty at start");

    /* --- round 1: lock, check, unlock, check empty --- */
    errno = EDOM;
    rc = pthread_mutex_lock(&m);
    TEST_CHECK(rc == 0, "pthread_mutex_lock rc == 0");
    TEST_CHECK(errno == EDOM, "errno preserved across lock");
    if (probe.available) {
        TEST_CHECK(probe.count() == 1, "tracker holds 1 mutex");
        TEST_CHECK(probe.holds(&m) == 1, "tracker holds THIS mutex");
        TEST_CHECK(probe.depth(&m) == 1, "depth is 1");
    }

    errno = EDOM;
    rc = pthread_mutex_unlock(&m);
    TEST_CHECK(rc == 0, "pthread_mutex_unlock rc == 0");
    TEST_CHECK(errno == EDOM, "errno preserved across unlock");
    if (probe.available) {
        TEST_CHECK(probe.count() == 0, "tracker EMPTY after release");
        TEST_CHECK(probe.holds(&m) == 0, "mutex no longer held");
    }

    /* --- round 2: same cycle again from a clean state --- */
    rc = pthread_mutex_lock(&m);
    TEST_CHECK(rc == 0, "second lock rc == 0");
    if (probe.available)
        TEST_CHECK(probe.count() == 1, "count 1 on second acquire");
    rc = pthread_mutex_unlock(&m);
    TEST_CHECK(rc == 0, "second unlock rc == 0");
    if (probe.available)
        TEST_CHECK(probe.count() == 0, "empty again after round 2");

    printf("TEST t10 PASS\n");
    return EXIT_SUCCESS;
}
