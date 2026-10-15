/*
 * tests/integration/t13_failed_ops.c - Phase 2 integration test.
 *
 * Deterministic FAILED operations on an error-checking mutex:
 *
 *   1. lock(m) succeeds (depth 1),
 *   2. lock(m) again by the same thread -> EDEADLK (defined, never
 *      blocks - this is the deterministic failed LOCK),
 *   3. unlock(m) succeeds ONCE: the tracker must be EMPTY afterwards,
 *      proving the failed re-lock added nothing (req 6),
 *   4. unlock(m) again while not owning it -> EPERM (defined, failed
 *      UNLOCK): tracker must stay empty, return code preserved (req 7).
 *
 * These error codes are identical with and without LD_PRELOAD, so the
 * binary doubles as a transparency check.
 */

#include "tracker_probe.h"

#include <errno.h>   /* EDEADLK, EPERM */
#include <pthread.h>

int main(void)
{
    tracker_probe probe;
    pthread_mutex_t em; /* error-checking mutex: defined failure codes */
    pthread_mutexattr_t attr;
    int rc;

    tracker_probe_load(&probe);

    rc = pthread_mutexattr_init(&attr);
    TEST_CHECK(rc == 0, "pthread_mutexattr_init");
    rc = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    TEST_CHECK(rc == 0, "pthread_mutexattr_settype(ERRORCHECK)");
    rc = pthread_mutex_init(&em, &attr);
    TEST_CHECK(rc == 0, "pthread_mutex_init");
    rc = pthread_mutexattr_destroy(&attr);
    TEST_CHECK(rc == 0, "pthread_mutexattr_destroy");

    if (probe.available)
        TEST_CHECK(probe.count() == 0, "tracker empty at start");

    /* 1. valid lock */
    rc = pthread_mutex_lock(&em);
    TEST_CHECK(rc == 0, "first lock rc == 0");
    if (probe.available) {
        TEST_CHECK(probe.count() == 1, "tracker holds the mutex");
        TEST_CHECK(probe.depth(&em) == 1, "depth 1");
    }

    /* 2. deterministic FAILED lock: re-locking an error-checking mutex
     *    from the owner returns EDEADLK immediately. */
    rc = pthread_mutex_lock(&em);
    TEST_CHECK(rc == EDEADLK, "re-lock fails with EDEADLK");
    if (probe.available) {
        TEST_CHECK(probe.depth(&em) == 1,
                   "failed lock did NOT increase depth (req 6)");
        TEST_CHECK(probe.count() == 1, "count unchanged after failed lock");
    }

    /* 3. single successful unlock must bring the tracker back to empty */
    rc = pthread_mutex_unlock(&em);
    TEST_CHECK(rc == 0, "unlock rc == 0");
    if (probe.available)
        TEST_CHECK(probe.count() == 0,
                   "empty after ONE unlock - failed re-lock added nothing");

    /* 4. deterministic FAILED unlock: not the owner -> EPERM.  The
     *    tracker must not be touched, and the error code must reach the
     *    caller unchanged. */
    rc = pthread_mutex_unlock(&em);
    TEST_CHECK(rc == EPERM, "stray unlock fails with EPERM");
    if (probe.available)
        TEST_CHECK(probe.count() == 0, "still empty after failed unlock");

    rc = pthread_mutex_destroy(&em);
    TEST_CHECK(rc == 0, "pthread_mutex_destroy");

    printf("TEST t13 PASS\n");
    return EXIT_SUCCESS;
}
