/* Sequential inversion creates a historical cycle without deadlocking.
 * EDEADLK verifies attempted dependencies are recorded before failure. */
#include "tracker_probe.h"
#include "rdp/graph.h"
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
static pthread_mutex_t a = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t b = PTHREAD_MUTEX_INITIALIZER;
int main(void)
{
    size_t (*edges)(void) = (size_t (*)(void))dlsym(RTLD_DEFAULT, "rdp_graph_edge_count");
    int (*analyze)(void *,void *,void **,size_t *,void **,void **) =
        (int (*)(void *,void *,void **,size_t *,void **,void **))dlsym(RTLD_DEFAULT, "rdp_graph_cycle_for_edge");
    TEST_CHECK(!pthread_mutex_lock(&a), "lock a");
    TEST_CHECK(!pthread_mutex_lock(&b), "lock b");
    TEST_CHECK(!pthread_mutex_unlock(&b), "unlock b");
    TEST_CHECK(!pthread_mutex_unlock(&a), "unlock a");
    if (edges) TEST_CHECK(edges() == 1, "one pre-acquisition dependency");
    TEST_CHECK(!pthread_mutex_lock(&b), "lock b first");
    errno = EDOM;
    TEST_CHECK(!pthread_mutex_lock(&a), "inverted acquisition");
    TEST_CHECK(errno == EDOM, "errno preserved through graph analysis");
    TEST_CHECK(!pthread_mutex_unlock(&a), "unlock a");
    TEST_CHECK(!pthread_mutex_unlock(&b), "unlock b");
    if (edges) {
        TEST_CHECK(edges() == 2, "two unique edges; graph mutex never tracked");
        TEST_CHECK(analyze(&b, &a, NULL, NULL, NULL, NULL) == RDP_GRAPH_ALREADY_REPORTED,
                   "wrapper detected inversion before return");
    }
    pthread_mutexattr_t attr;
    pthread_mutex_t errorcheck;
    TEST_CHECK(!pthread_mutexattr_init(&attr), "attr init");
    TEST_CHECK(!pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK), "errorcheck attr");
    TEST_CHECK(!pthread_mutex_init(&errorcheck, &attr), "mutex init");
    TEST_CHECK(!pthread_mutex_lock(&errorcheck), "first acquisition");
    TEST_CHECK(pthread_mutex_lock(&errorcheck) == EDEADLK, "original failure preserved");
    if (edges) TEST_CHECK(edges() == 3, "failed attempt recorded before real call");
    TEST_CHECK(!pthread_mutex_unlock(&errorcheck), "unlock");
    TEST_CHECK(!pthread_mutex_destroy(&errorcheck), "destroy");
    TEST_CHECK(!pthread_mutexattr_destroy(&attr), "attr destroy");
    puts("TEST t21 PASS");
    return 0;
}
