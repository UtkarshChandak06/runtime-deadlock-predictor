/* Link-time dlsym fault injection: no production test configuration. */
#include "rdp/internal.h"
#include "rdp/graph.h"
#include "rdp/tracker.h"
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *__real_dlsym(void *handle, const char *name);
static pthread_mutex_t probe_mutex = PTHREAD_MUTEX_INITIALIZER;
static int reentered;
static int recursive_rc, recursive_errno;

void *__wrap_dlsym(void *handle, const char *name)
{
    const char *mode = getenv("RDP_TEST_RESOLVER");
    if (mode && !strcmp(mode, "reenter") && !reentered) {
        reentered = 1;
        errno = EDOM;
        recursive_rc = pthread_mutex_lock(&probe_mutex);
        recursive_errno = errno;
    }
    if (mode && ((!strcmp(mode, "lock") && !strcmp(name, "pthread_mutex_lock")) ||
                 (!strcmp(mode, "unlock") && !strcmp(name, "pthread_mutex_unlock"))))
        return NULL;
    return __real_dlsym(handle, name);
}

static void *failed_caller(void *unused)
{
    (void)unused;
    void *path[RDP_GRAPH_MAX_CYC], *from = (void *)1, *to = (void *)1;
    size_t len = 123;
    for (int i = 0; i < 4; ++i) {
        errno = EDOM;
        assert(pthread_mutex_lock(&probe_mutex) == ENOSYS);
        assert(errno == ENOSYS);
        assert(pthread_mutex_unlock(&probe_mutex) == ENOSYS);
        assert(rdp_tracker_count() == 0);
        assert(rdp_graph_cycle_for_edge(&probe_mutex, &len, path, &len, &from, &to) == RDP_GRAPH_ANALYSIS_INCOMPLETE);
        assert(len == 0 && from == NULL && to == NULL);
        assert(rdp_graph_node_count() == 0 && rdp_graph_edge_count() == 0);
        assert(rdp_graph_dropped() == SIZE_MAX);
        rdp_log("must not enter unavailable logger synchronization\n");
        assert(rdp_log_errors() == SIZE_MAX && rdp_log_dropped() == SIZE_MAX);
    }
    return NULL;
}

int main(void)
{
    const char *mode = getenv("RDP_TEST_RESOLVER");
    assert(mode);
    if (!strcmp(mode, "reenter")) {
        assert(reentered && recursive_rc == ENOSYS && recursive_errno == ENOSYS);
        errno = EDOM;
        assert(!pthread_mutex_lock(&probe_mutex));
        assert(errno == EDOM && rdp_tracker_count() == 1);
        assert(!pthread_mutex_unlock(&probe_mutex));
        assert(errno == EDOM && rdp_tracker_count() == 0);
        puts("PASS resolver reentry: bounded ENOSYS, normal forwarding recovers");
    } else {
        pthread_t threads[4];
        assert(rdp_monitor_enter() == -1);
        for (size_t i = 0; i < 4; ++i) assert(!pthread_create(&threads[i], NULL, failed_caller, NULL));
        for (size_t i = 0; i < 4; ++i) assert(!pthread_join(threads[i], NULL));
        puts("PASS incomplete resolver pair: ENOSYS, no graph/tracker mutation, safe concurrent calls");
    }
    return 0;
}
