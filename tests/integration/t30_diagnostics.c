#include "tracker_probe.h"
#include "rdp/graph.h"
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
static pthread_mutex_t locks[RDP_GRAPH_MAX_NODES + 2];
static pthread_barrier_t barrier;
static size_t (*edges)(void);
static size_t (*drops)(void);
static int (*add)(void *, void *);
static void pair(size_t a, size_t b)
{
    errno = EDOM;
    TEST_CHECK(!pthread_mutex_lock(&locks[a]), "first lock");
    TEST_CHECK(!pthread_mutex_lock(&locks[b]), "second lock");
    TEST_CHECK(errno == EDOM, "lock errno");
    TEST_CHECK(!pthread_mutex_unlock(&locks[b]), "second unlock");
    TEST_CHECK(!pthread_mutex_unlock(&locks[a]), "first unlock");
    TEST_CHECK(errno == EDOM, "unlock errno");
}
static void expected(size_t start, size_t n)
{
    /* Called before concurrent work: one printf per expected record. */
    char line[30000];
    size_t off = (size_t)snprintf(line, sizeof line, "EXPECT tid=%ld held=%p requested=%p cycle=",
        (long)syscall(SYS_gettid), (void *)&locks[start+n-1], (void *)&locks[start]);
    for (size_t i = 0; i <= n; ++i)
        off += (size_t)snprintf(line+off, sizeof line-off, "%s%p", i ? " -> " : "", (void *)&locks[start+i%n]);
    printf("%s\n", line);
}
static void *worker(void *arg)
{
    size_t base = (size_t)arg * 3;
    pair(base, base+1);
    pair(base+1, base+2);
    expected(base, 3);
    int rc = pthread_barrier_wait(&barrier);
    TEST_CHECK(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD, "barrier");
    for (int i = 0; i < 20; ++i) pair(base+2, base);
    return NULL;
}
static void *shared(void *unused)
{
    (void)unused;
    for (int i = 0; i < 100; ++i) {
        TEST_CHECK(!pthread_mutex_lock(&locks[0]), "shared lock");
        TEST_CHECK(!pthread_mutex_unlock(&locks[0]), "shared unlock");
    }
    return NULL;
}
int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "two";
    tracker_probe p;
    tracker_probe_load(&p);
    edges = (size_t (*)(void))dlsym(RTLD_DEFAULT, "rdp_graph_edge_count");
    drops = (size_t (*)(void))dlsym(RTLD_DEFAULT, "rdp_graph_dropped");
    add = (int (*)(void *, void *))dlsym(RTLD_DEFAULT, "rdp_graph_add_edge");
    for (size_t i = 0; i < RDP_GRAPH_MAX_NODES+2; ++i)
        TEST_CHECK(!pthread_mutex_init(&locks[i], NULL), "init");
    if (!strcmp(mode, "shared") || !strcmp(mode, "concurrent")) {
        pthread_t threads[8];
        TEST_CHECK(!pthread_barrier_init(&barrier, NULL, 8), "barrier init");
        for (size_t i = 0; i < 8; ++i)
            TEST_CHECK(!pthread_create(&threads[i], NULL, !strcmp(mode,"shared") ? shared : worker, (void *)i), "create");
        for (int i = 0; i < 8; ++i) TEST_CHECK(!pthread_join(threads[i], NULL), "join");
        if (edges) TEST_CHECK(edges() == (!strcmp(mode,"shared") ? 0 : 24), "unique edges");
        TEST_CHECK(!pthread_barrier_destroy(&barrier), "barrier destroy");
    } else if (!strcmp(mode, "new")) {
        pair(0, 1); expected(0, 2); pair(1, 0);
        pair(1, 2); expected(0, 3); pair(2, 0);
        if (edges) TEST_CHECK(edges() == 4, "new cycle information retained");
    } else if (!strcmp(mode, "tracker")) {
        for (size_t i = 0; i < 65; ++i) TEST_CHECK(!pthread_mutex_lock(&locks[i]), "capacity lock");
        if (p.available) TEST_CHECK(p.count() == 64 && p.dropped() == 1, "tracker limit");
        for (size_t i = 65; i; --i) TEST_CHECK(!pthread_mutex_unlock(&locks[i-1]), "capacity unlock");
    } else if (!strcmp(mode, "nodes") || !strcmp(mode, "edges")) {
        if (add) {
            if (!strcmp(mode,"nodes")) {
                for (size_t i = 0; i < 1023; ++i) TEST_CHECK(add(&locks[i], &locks[i+1]) == 0, "fill nodes");
            } else {
                for (size_t i = 0; i < 64; ++i)
                    for (size_t j = 0; j < 64; ++j) TEST_CHECK(add(&locks[i], &locks[j]) == 0, "fill edges");
            }
        }
        pair(0, 1024);
        if (drops) TEST_CHECK(drops() == 1, "one rejected dependency");
    } else {
        size_t n = !strcmp(mode,"long") ? 1024 : !strcmp(mode,"three") ? 3 : 2;
        for (size_t i = 0; i+1 < n; ++i) pair(i, i+1);
        expected(0, n);
        for (int i = 0; i < 20; ++i) pair(n-1, 0);
        if (edges) TEST_CHECK(edges() == n, "duplicates do not inflate graph");
    }
    if (p.available) TEST_CHECK(p.count() == 0, "tracker empty after diagnostics");
    if (!strcmp(mode, "failure") && edges) {
        size_t (*errors)(void) = (size_t (*)(void))dlsym(RTLD_DEFAULT, "rdp_log_errors");
        TEST_CHECK(errors && errors() > 0, "logging failure counted");
    }
    puts("TEST t30 PASS");
    return 0;
}


