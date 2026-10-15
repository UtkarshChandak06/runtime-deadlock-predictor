/*
 * t00_smoke.c - Phase 1 smoke test (integration).
 *
 * Two threads acquire two mutexes in the SAME order (m1 -> m2) and
 * release them in reverse order, several times. Because every thread
 * uses the same lock order, no deadlock is possible and the program
 * must always finish:
 *
 *   ./build/t00_smoke
 *       -> prints "TEST t00 PASS", exits 0
 *
 *   LD_PRELOAD=$PWD/build/libdeadlock_monitor.so ./build/t00_smoke
 *       -> same result, PLUS interception diagnostics on stderr
 *
 * The test proves two things about the wrappers:
 *   - transparency: identical output, exit code and counter value with
 *     and without the monitor (return values and errno preserved),
 *   - liveness: the interception diagnostics actually appear, i.e. the
 *     wrappers fire and do not recurse or deadlock.
 *
 * Expected outcome in docs/TESTING_PLAN.md: "none" (exit 0, no
 * "POTENTIAL DEADLOCK" warning - analysis does not exist yet).
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define NTHREADS    2
#define NITERATIONS 5

static pthread_mutex_t m1 = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t m2 = PTHREAD_MUTEX_INITIALIZER;
static long counter; /* protected by m1 */

static void die(const char *what, int rc)
{
    fprintf(stderr, "t00_smoke: %s failed (rc=%d)\n", what, rc);
    exit(EXIT_FAILURE);
}

static void *worker(void *arg)
{
    long id = (long)arg;

    for (int i = 0; i < NITERATIONS; i++) {
        int rc = pthread_mutex_lock(&m1);
        if (rc != 0)
            die("pthread_mutex_lock(m1)", rc);

        rc = pthread_mutex_lock(&m2);
        if (rc != 0)
            die("pthread_mutex_lock(m2)", rc);

        counter++; /* critical section */

        rc = pthread_mutex_unlock(&m2);
        if (rc != 0)
            die("pthread_mutex_unlock(m2)", rc);

        rc = pthread_mutex_unlock(&m1);
        if (rc != 0)
            die("pthread_mutex_unlock(m1)", rc);
    }

    printf("t00_smoke: thread %ld finished\n", id);
    return NULL;
}

int main(void)
{
    pthread_t threads[NTHREADS];
    int rc;

    for (long i = 0; i < NTHREADS; i++) {
        rc = pthread_create(&threads[i], NULL, worker, (void *)i);
        if (rc != 0)
            die("pthread_create", rc);
    }

    for (int i = 0; i < NTHREADS; i++) {
        rc = pthread_join(threads[i], NULL);
        if (rc != 0)
            die("pthread_join", rc);
    }

    if (counter != NTHREADS * NITERATIONS) {
        fprintf(stderr, "t00_smoke: FAIL - counter=%ld expected=%d\n",
                counter, NTHREADS * NITERATIONS);
        return EXIT_FAILURE;
    }

    printf("TEST t00 PASS (counter=%ld)\n", counter);
    return EXIT_SUCCESS;
}
