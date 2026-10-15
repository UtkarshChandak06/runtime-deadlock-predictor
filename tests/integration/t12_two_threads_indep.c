/*
 * tests/integration/t12_two_threads_indep.c - Phase 2 integration test.
 *
 * Two threads each lock and release THEIR OWN mutex, concurrently and
 * repeatedly. Each thread asserts - while holding its own lock - that
 * its tracker view contains exactly ITS mutex (req 11: one thread can
 * never see or modify another thread's held-lock collection), and that
 * the main thread's view stays empty throughout.
 */

#include "tracker_probe.h"

#include <pthread.h>

#define NITERATIONS 10

static pthread_mutex_t m0 = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t m1 = PTHREAD_MUTEX_INITIALIZER;

static void *worker(void *arg)
{
    long id = (long)arg;
    pthread_mutex_t *mine = (id == 0) ? &m0 : &m1;
    pthread_mutex_t *theirs = (id == 0) ? &m1 : &m0;
    tracker_probe probe;

    tracker_probe_load(&probe);
    if (probe.available)
        TEST_CHECK(probe.count() == 0, "fresh thread starts with empty set");

    for (int i = 0; i < NITERATIONS; i++) {
        int rc = pthread_mutex_lock(mine);
        TEST_CHECK(rc == 0, "worker lock rc == 0");

        if (probe.available) {
            TEST_CHECK(probe.count() == 1,
                       "own set has exactly one entry while holding");
            TEST_CHECK(probe.holds(mine) == 1, "holds its own mutex");
            TEST_CHECK(probe.holds(theirs) == 0,
                       "cannot see the other thread's mutex");
        }

        rc = pthread_mutex_unlock(mine);
        TEST_CHECK(rc == 0, "worker unlock rc == 0");

        if (probe.available)
            TEST_CHECK(probe.count() == 0, "empty again after each release");
    }

    printf("t12: thread %ld finished\n", id);
    return NULL;
}

int main(void)
{
    tracker_probe probe;
    pthread_t threads[2];
    int rc;

    tracker_probe_load(&probe);
    if (probe.available)
        TEST_CHECK(probe.count() == 0, "main thread holds nothing at start");

    for (long i = 0; i < 2; i++) {
        rc = pthread_create(&threads[i], NULL, worker, (void *)i);
        TEST_CHECK(rc == 0, "pthread_create");
    }
    for (int i = 0; i < 2; i++) {
        rc = pthread_join(threads[i], NULL);
        TEST_CHECK(rc == 0, "pthread_join");
    }

    if (probe.available)
        TEST_CHECK(probe.count() == 0,
                   "main thread's set untouched by workers");

    printf("TEST t12 PASS\n");
    return EXIT_SUCCESS;
}
