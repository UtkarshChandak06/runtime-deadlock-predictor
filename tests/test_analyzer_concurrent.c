#define _XOPEN_SOURCE 700
#include "analysis_test.h"
#include <stdatomic.h>

enum { WORKERS = 8, ITERATIONS = 100 };
typedef struct {
    RdpGraph *graph;
    RdpTracker tracker;
    pthread_barrier_t *start;
    atomic_uint *warnings;
    int number;
} Work;
static void *worker(void *argument)
{
    Work *work = argument;
    int result = pthread_barrier_wait(work->start);
    CHECK(result == 0 || result == PTHREAD_BARRIER_SERIAL_THREAD);
    RdpLockId first = work->number % 2 ? 1 : 2, second = 3 - first;
    for (int i = 0; i < ITERATIONS; ++i) {
        acquire(work->graph, &work->tracker, first, RDP_NORMAL);
        RdpAnalysis a = request(work->graph, &work->tracker, second, RDP_NORMAL);
        for (size_t j = 0; j < a.warning_count; ++j) {
            cycle_is(&a.warnings[j], (RdpLockId[]){second,first,second}, 3);
            CHECK(a.warnings[j].thread == (RdpThreadId)work->number + 1);
            CHECK(a.warnings[j].sequence == 2);
        }
        atomic_fetch_add(work->warnings, (unsigned)a.warning_count);
        OK(rdp_record_lock_acquired(&work->tracker, second, 0));
        OK(rdp_record_lock_released(&work->tracker, second, 0));
        OK(rdp_record_lock_released(&work->tracker, first, 0));
    }
    return NULL;
}
int main(void)
{
    RdpGraph g = {0}; graph_init(&g);
    pthread_barrier_t start; CHECK(pthread_barrier_init(&start, NULL, WORKERS + 1) == 0);
    atomic_uint warnings = 0;
    Work work[WORKERS] = {0}; pthread_t threads[WORKERS];
    for (int i = 0; i < WORKERS; ++i) {
        work[i].graph = &g; work[i].start = &start; work[i].warnings = &warnings;
        work[i].number = i; tracker_init(&work[i].tracker, (RdpThreadId)i + 1);
        CHECK(pthread_create(&threads[i], NULL, worker, &work[i]) == 0);
    }
    int result = pthread_barrier_wait(&start);
    CHECK(result == 0 || result == PTHREAD_BARRIER_SERIAL_THREAD);
    for (int i = 0; i < ITERATIONS; ++i) {
        RdpGraphStats stats; OK(rdp_graph_stats(&g, &stats)); CHECK(stats.complete && stats.edges <= 2);
        for (int j = 0; j < WORKERS; ++j) {
            RdpSnapshot s; OK(rdp_tracker_snapshot(&work[j].tracker, &s));
            CHECK(s.count <= 2 && s.thread == (RdpThreadId)j + 1);
        }
    }
    for (int i = 0; i < WORKERS; ++i) {
        CHECK(pthread_join(threads[i], NULL) == 0);
        RdpSnapshot s; OK(rdp_tracker_snapshot(&work[i].tracker, &s)); CHECK(s.count == 0 && s.pending == 0);
        OK(rdp_tracker_destroy(&work[i].tracker));
    }
    RdpGraphStats stats; OK(rdp_graph_stats(&g, &stats));
    CHECK(stats.nodes == 2 && stats.edges == 2 && stats.sequence == 2 && stats.complete);
    CHECK(atomic_load(&warnings) == 1);
    CHECK(pthread_barrier_destroy(&start) == 0); OK(rdp_graph_destroy(&g));
    puts("PASS concurrent: 8 workers x 100 event sequences, concurrent snapshots, one closing-edge warning");
    return 0;
}
