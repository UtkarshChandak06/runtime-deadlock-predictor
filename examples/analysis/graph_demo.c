#include "rdp/analyzer.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static void check(RdpStatus status)
{
    if (status != RDP_OK) { fprintf(stderr, "analysis failed: status=%d\n", status); exit(1); }
}
static size_t supplied_order(RdpGraph *graph, RdpThreadId thread, RdpLockId first, RdpLockId second)
{
    RdpTracker tracker = {0}; RdpAnalysis result;
    check(rdp_tracker_init(&tracker, thread, RDP_MAX_HELD, UINT32_MAX));
    check(rdp_analyze_lock_request(graph, &tracker, first, RDP_NORMAL, &result));
    check(rdp_record_lock_acquired(&tracker, first, 0));
    check(rdp_analyze_lock_request(graph, &tracker, second, RDP_NORMAL, &result));
    for (size_t i = 0; i < result.warning_count; ++i) {
        const RdpWarning *w = &result.warnings[i];
        printf("type=potential_lock_order_cycle thread=%" PRIu64
               " held_lock=%" PRIu64 " requested_lock=%" PRIu64
               " closing_edge=%" PRIu64 "->%" PRIu64 " sequence=%" PRIu64 " cycle=",
               w->thread, w->held_lock, w->requested_lock,
               w->held_lock, w->requested_lock, w->sequence);
        for (size_t j = 0; j < w->cycle_length; ++j)
            printf("%s%" PRIu64, j ? "->" : "", w->cycle[j]);
        putchar('\n');
    }
    check(rdp_record_lock_acquired(&tracker, second, 0));
    check(rdp_record_lock_released(&tracker, second, 0));
    check(rdp_record_lock_released(&tracker, first, 0));
    check(rdp_tracker_destroy(&tracker));
    return result.warning_count;
}
int main(void)
{
    RdpGraph graph = {0};
    check(rdp_graph_init(&graph, RDP_MAX_NODES, RDP_MAX_EDGES, RDP_MAX_PATH));
    puts("Phase 3: supplied events drive the real analysis API; no live interception.");
    puts("A warning means potential risk, not proof of a currently blocked application.");
    size_t warnings = supplied_order(&graph, 101, 10, 20);
    warnings += supplied_order(&graph, 202, 10, 20);
    printf("safe_order warnings=%zu\n", warnings);
    warnings = supplied_order(&graph, 303, 20, 10);
    printf("opposite_order warnings=%zu\n", warnings);
    check(rdp_graph_reset(&graph));
    warnings = supplied_order(&graph, 404, 10, 20);
    warnings += supplied_order(&graph, 505, 20, 30);
    warnings += supplied_order(&graph, 606, 30, 10);
    printf("three_lock_order warnings=%zu\n", warnings);
    check(rdp_graph_destroy(&graph));
    return 0;
}
