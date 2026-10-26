#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "fixture failure line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static pthread_mutex_t locks[65];
static void announce(const char *label, pthread_mutex_t *mutex)
{ printf("OBSERVED label=%s tid=%ld address=%p\n", label, (long)syscall(SYS_gettid), (void *)mutex); }
static void take(pthread_mutex_t *mutex)
{
    errno = EDOM; CHECK(pthread_mutex_lock(mutex) == 0); CHECK(errno == EDOM);
}
static void drop(pthread_mutex_t *mutex)
{
    errno = ERANGE; CHECK(pthread_mutex_unlock(mutex) == 0); CHECK(errno == ERANGE);
}
static void pair(int first, int second)
{ take(&locks[first]); take(&locks[second]); drop(&locks[second]); drop(&locks[first]); }
static void *wrong_unlock(void *argument)
{
    pthread_mutex_t *mutex = argument;
    announce("wrong_unlock", mutex);
    int result = pthread_mutex_unlock(mutex);
    printf("WRONG_UNLOCK result=%d\n", result); CHECK(result == EPERM);
    return NULL;
}
static void *concurrent(void *argument)
{
    (void)argument; announce("worker", &locks[0]);
    for (int i = 0; i < 20; ++i) pair(0, 1);
    return NULL;
}
static atomic_bool cancel_ready, cancel_go;
static void *robust_owner(void *argument)
{
    (void)argument; take(&locks[0]); return NULL; /* Explicit unsupported owner-death fixture. */
}
static void *cancel_worker(void *argument)
{
    (void)argument;
    CHECK(pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL) == 0);
    atomic_store(&cancel_ready, true);
    while (!atomic_load(&cancel_go)) { }
    take(&locks[0]); drop(&locks[0]);
    CHECK(pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL) == 0);
    pthread_testcancel();
    return NULL;
}
int main(int argc, char **argv)
{
    CHECK(argc == 2); CHECK(setvbuf(stdout, NULL, _IONBF, 0) == 0);
    const char *mode = argv[1];
    if (strcmp(mode, "cycle3") == 0 || strcmp(mode, "cycle8") == 0) {
        int count = strcmp(mode, "cycle3") == 0 ? 3 : 8;
        for (int i=0; i<count; ++i) CHECK(pthread_mutex_init(&locks[i], NULL) == 0);
        /* Real sequential pair acquisitions: historical cycle, no actual deadlock. */
        for (int i=0; i<count; ++i) pair(i, (i+1)%count);
        for (int i=0; i<count; ++i) CHECK(pthread_mutex_destroy(&locks[i]) == 0);
    } else if (strcmp(mode, "static") == 0 || strcmp(mode, "static-recursive") == 0) {
        static pthread_mutex_t normal = PTHREAD_MUTEX_INITIALIZER;
        static pthread_mutex_t recursive = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
        pthread_mutex_t *mutex = strcmp(mode, "static") == 0 ? &normal : &recursive;
        announce(mode, mutex); take(mutex);
        if (mutex == &recursive) { take(mutex); drop(mutex); }
        drop(mutex); CHECK(pthread_mutex_destroy(mutex) == 0);
    } else if (strcmp(mode, "recursive") == 0 || strcmp(mode, "errorcheck") == 0 ||
               strcmp(mode, "robust") == 0 || strcmp(mode, "owner-dead") == 0) {
        pthread_mutexattr_t attr; CHECK(pthread_mutexattr_init(&attr) == 0);
        bool recursive = strcmp(mode, "recursive") == 0;
        bool owner_dead = strcmp(mode, "owner-dead") == 0;
        bool robust = strcmp(mode, "robust") == 0 || owner_dead;
        CHECK(pthread_mutexattr_settype(&attr, recursive ? PTHREAD_MUTEX_RECURSIVE : PTHREAD_MUTEX_ERRORCHECK) == 0);
        if (robust) CHECK(pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST) == 0);
        CHECK(pthread_mutex_init(&locks[0], &attr) == 0); CHECK(pthread_mutexattr_destroy(&attr) == 0);
        announce(mode, &locks[0]);
        if (owner_dead) {
            pthread_t owner; CHECK(pthread_create(&owner, NULL, robust_owner, NULL) == 0);
            CHECK(pthread_join(owner, NULL) == 0);
            int result = pthread_mutex_lock(&locks[0]);
            printf("OWNER_DEAD result=%d\n", result); CHECK(result == EOWNERDEAD);
            CHECK(pthread_mutex_consistent(&locks[0]) == 0);
        } else take(&locks[0]);
        if (recursive) { take(&locks[0]); drop(&locks[0]); }
        else if (!robust) {
            errno = EDOM; int result = pthread_mutex_lock(&locks[0]);
            printf("SELF_LOCK result=%d\n", result); CHECK(result == EDEADLK && errno == EDOM);
            pthread_t other; CHECK(pthread_create(&other, NULL, wrong_unlock, &locks[0]) == 0);
            CHECK(pthread_join(other, NULL) == 0);
        }
        drop(&locks[0]); CHECK(pthread_mutex_destroy(&locks[0]) == 0);
    } else if (strcmp(mode, "reuse") == 0) {
        for (int i = 0; i < 3; ++i) {
            CHECK(pthread_mutex_init(&locks[0], NULL) == 0);
            announce(mode, &locks[0]); take(&locks[0]); drop(&locks[0]);
            CHECK(pthread_mutex_destroy(&locks[0]) == 0);
        }
    } else if (strcmp(mode, "nodes") == 0 || strcmp(mode, "registry") == 0) {
        int count = strcmp(mode, "nodes") == 0 ? 33 : 65;
        for (int i = 0; i < count; ++i) {
            CHECK(pthread_mutex_init(&locks[i], NULL) == 0);
            if (count == 33) { take(&locks[i]); drop(&locks[i]); }
        }
        for (int i = 0; i < count; ++i) CHECK(pthread_mutex_destroy(&locks[i]) == 0);
    } else {
        CHECK(pthread_mutex_init(&locks[0], NULL) == 0);
        CHECK(pthread_mutex_init(&locks[1], NULL) == 0);
        announce("first", &locks[0]); announce("second", &locks[1]);
        if (strcmp(mode, "duplicate") == 0) {
            for (int i = 0; i < 5; ++i) { pair(0, 1); pair(1, 0); }
        } else if (strcmp(mode, "concurrent") == 0 || strcmp(mode, "threads") == 0) {
            pthread_t threads[6];
            for (int i = 0; i < 6; ++i) {
                CHECK(pthread_create(&threads[i], NULL, concurrent, NULL) == 0);
                if (strcmp(mode, "threads") == 0) CHECK(pthread_join(threads[i], NULL) == 0);
            }
            if (strcmp(mode, "concurrent") == 0)
                for (int i = 0; i < 6; ++i) CHECK(pthread_join(threads[i], NULL) == 0);
        } else if (strcmp(mode, "trylock") == 0) {
            CHECK(pthread_mutex_trylock(&locks[0]) == 0); drop(&locks[0]);
        } else if (strcmp(mode, "cancel") == 0) {
            pthread_t thread; CHECK(pthread_create(&thread, NULL, cancel_worker, NULL) == 0);
            while (!atomic_load(&cancel_ready)) { }
            CHECK(pthread_cancel(thread) == 0); atomic_store(&cancel_go, true);
            void *result; CHECK(pthread_join(thread, &result) == 0); CHECK(result == PTHREAD_CANCELED);
            pair(0, 1);
        } else CHECK(0);
        CHECK(pthread_mutex_destroy(&locks[0]) == 0); CHECK(pthread_mutex_destroy(&locks[1]) == 0);
    }
    puts("FIXTURE PASS"); return 0;
}
