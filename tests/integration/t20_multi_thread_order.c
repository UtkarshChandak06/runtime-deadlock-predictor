/*
 * tests/integration/t20_multi_thread_order.c - Phase 3 integration test
 * (Test F at the wrapper level + regression guard for t00/t10-t13).
 *
 * Many threads repeatedly acquire several mutexes in ONE consistent
 * global order. Consistent order => the lock-order graph stays a DAG
 * => the monitor must NEVER report "POTENTIAL DEADLOCK" and every
 * thread must finish. This validates:
 *   - graph updates under real concurrency (no corruption, no crash),
 *   - no false positives on a cycle-free program,
 *   - no regression of the Phase 1/2 wrapper behaviour.
 */

#include "tracker_probe.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define NTHREADS 4
#define NLOCKS   4
#define NROUNDS  20

static pthread_mutex_t locks[NLOCKS];

static void *worker(void *arg)
{
    long id = (long)arg;

    for (int r = 0; r < NROUNDS; r++) {
        for (int i = 0; i < NLOCKS; i++) {
            int rc = pthread_mutex_lock(&locks[i]);
            TEST_CHECK(rc == 0, "lock in global order returns 0");
        }
        for (int i = NLOCKS; i > 0; i--) {
            int rc = pthread_mutex_unlock(&locks[i - 1]);
            TEST_CHECK(rc == 0, "unlock in reverse order returns 0");
        }
    }
    printf("t20: thread %ld finished\n", id);
    return NULL;
}

int main(void)
{
    pthread_t th[NTHREADS];
    tracker_probe probe;

    tracker_probe_load(&probe);
    for (int i = 0; i < NLOCKS; i++) {
        pthread_mutex_init(&locks[i], NULL);
    }

    for (long i = 0; i < NTHREADS; i++) {
        int rc = pthread_create(&th[i], NULL, worker, (void *)i);
        TEST_CHECK(rc == 0, "pthread_create");
    }
    for (int i = 0; i < NTHREADS; i++) {
        int rc = pthread_join(th[i], NULL);
        TEST_CHECK(rc == 0, "pthread_join");
    }

    for (int i = 0; i < NLOCKS; i++) {
        TEST_CHECK(pthread_mutex_destroy(&locks[i]) == 0, "destroy");
    }

    printf("TEST t20 PASS\n");
    return EXIT_SUCCESS;
}
