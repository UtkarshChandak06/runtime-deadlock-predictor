#include "analysis_test.h"

static void banking_sequence(RdpGraph *g, RdpTracker *t, RdpLockId first, RdpLockId second, size_t warnings)
{
    RdpAnalysis a = request(g, t, first, RDP_NORMAL); CHECK(a.warning_count == 0);
    OK(rdp_record_lock_acquired(t, first, 0));
    a = request(g, t, second, RDP_NORMAL); CHECK(a.warning_count == warnings);
    if (warnings) cycle_is(&a.warnings[0], (RdpLockId[]){second,first,second}, 3);
    OK(rdp_record_lock_acquired(t, second, 0));
    OK(rdp_record_lock_released(t, second, 0)); OK(rdp_record_lock_released(t, first, 0));
}
int main(void)
{
    RdpGraph g = {0}; RdpTracker t = {0}, u = {0}; RdpAnalysis a;
    RdpSnapshot s; RdpGraphStats stats; bool found;
    CHECK(rdp_analyze_lock_request(&g, &t, 1, RDP_NORMAL, &a) == RDP_UNINITIALIZED);
    graph_init(&g); tracker_init(&t, 123); tracker_init(&u, 456);
    CHECK(rdp_analyze_lock_request(&g, &t, 0, RDP_NORMAL, &a) == RDP_INVALID);
    banking_sequence(&g, &t, 1, 2, 0); banking_sequence(&g, &u, 1, 2, 0);
    banking_sequence(&g, &u, 2, 1, 1); banking_sequence(&g, &u, 2, 1, 0);
    OK(rdp_graph_reset(&g));
    acquire(&g, &t, 1, RDP_NORMAL);
    a = request(&g, &t, 2, RDP_NORMAL); CHECK(a.warning_count == 0);
    OK(rdp_tracker_snapshot(&t, &s)); CHECK(s.count == 1 && s.pending == 2);
    OK(rdp_graph_has_edge(&g, 1, 2, &found)); CHECK(found); /* before acquisition */
    OK(rdp_record_lock_acquired(&t, 2, EBUSY));
    OK(rdp_tracker_snapshot(&t, &s)); CHECK(s.count == 1);
    OK(rdp_record_lock_released(&t, 1, EPERM));
    OK(rdp_tracker_snapshot(&t, &s)); CHECK(s.count == 1);
    OK(rdp_record_lock_released(&t, 1, 0));
    acquire(&g, &u, 2, RDP_NORMAL);
    a = request(&g, &u, 1, RDP_NORMAL); CHECK(a.warning_count == 1);
    CHECK(a.warnings[0].thread == 456); /* failed request history still warns */
    OK(rdp_record_lock_acquired(&u, 1, EBUSY)); OK(rdp_record_lock_released(&u, 2, 0));
    OK(rdp_graph_reset(&g));
    acquire(&g, &t, 8, RDP_RECURSIVE); acquire(&g, &t, 9, RDP_NORMAL);
    a = request(&g, &t, 8, RDP_RECURSIVE); CHECK(a.warning_count == 0);
    OK(rdp_record_lock_acquired(&t, 8, 0));
    OK(rdp_graph_has_edge(&g, 9, 8, &found)); CHECK(!found);
    OK(rdp_graph_has_edge(&g, 8, 8, &found)); CHECK(!found);
    OK(rdp_record_lock_released(&t, 8, 0)); OK(rdp_record_lock_released(&t, 8, 0));
    OK(rdp_record_lock_released(&t, 9, 0));
    for (int kind = RDP_NORMAL; kind <= RDP_UNKNOWN; kind += 2) {
        OK(rdp_graph_reset(&g)); acquire(&g, &t, 55, (RdpLockKind)kind);
        a = request(&g, &t, 55, (RdpLockKind)kind); CHECK(a.warning_count == 1);
        cycle_is(&a.warnings[0], (RdpLockId[]){55,55}, 2);
        OK(rdp_record_lock_acquired(&t, 55, EDEADLK));
        OK(rdp_record_lock_released(&t, 55, 0));
    }
    /* Every held lock contributes, not only the most recently acquired one. */
    OK(rdp_graph_reset(&g));
    acquire(&g, &t, 1, RDP_NORMAL); acquire(&g, &t, 2, RDP_NORMAL);
    a = request(&g, &t, 3, RDP_NORMAL); CHECK(a.warning_count == 0);
    OK(rdp_graph_has_edge(&g, 1, 3, &found)); CHECK(found);
    OK(rdp_graph_has_edge(&g, 2, 3, &found)); CHECK(found);
    OK(rdp_record_lock_acquired(&t, 3, 0));
    for (RdpLockId i = 1; i <= 3; ++i) OK(rdp_record_lock_released(&t, i, 0));
    /* Reconstruct a full three-node path through the analyzer itself. */
    OK(rdp_graph_reset(&g));
    banking_sequence(&g, &t, 1, 2, 0); banking_sequence(&g, &t, 2, 3, 0);
    acquire(&g, &u, 3, RDP_NORMAL); a = request(&g, &u, 1, RDP_NORMAL);
    CHECK(a.warning_count == 1); cycle_is(&a.warnings[0], (RdpLockId[]){1,2,3,1}, 4);
    OK(rdp_record_lock_acquired(&u, 1, EBUSY)); OK(rdp_record_lock_released(&u, 3, 0));
    OK(rdp_graph_reset(&g));
    (void)edge(&g, 4, 1); (void)edge(&g, 4, 2);
    acquire(&g, &t, 1, RDP_NORMAL); acquire(&g, &t, 2, RDP_NORMAL);
    a = request(&g, &t, 4, RDP_NORMAL); CHECK(a.warning_count == 2);
    cycle_is(&a.warnings[0], (RdpLockId[]){4,1,4}, 3);
    cycle_is(&a.warnings[1], (RdpLockId[]){4,2,4}, 3);
    OK(rdp_record_lock_acquired(&t, 4, EBUSY));
    OK(rdp_record_lock_released(&t, 1, 0)); OK(rdp_record_lock_released(&t, 2, 0));
    /* A batch can retain an earlier valid warning but report incomplete when
     * its next dependency exceeds capacity. It must not pretend full coverage. */
    OK(rdp_graph_destroy(&g)); OK(rdp_graph_init(&g, 8, 4, RDP_MAX_PATH));
    (void)edge(&g, 4, 1); (void)edge(&g, 4, 2);
    acquire(&g, &t, 1, RDP_NORMAL); acquire(&g, &t, 2, RDP_NORMAL);
    CHECK(rdp_analyze_lock_request(&g, &t, 4, RDP_NORMAL, &a) == RDP_EDGE_LIMIT);
    CHECK(!a.complete && a.warning_count == 1);
    cycle_is(&a.warnings[0], (RdpLockId[]){4,1,4}, 3);
    OK(rdp_record_lock_acquired(&t, 4, EBUSY));
    OK(rdp_record_lock_released(&t, 1, 0)); OK(rdp_record_lock_released(&t, 2, 0));
    OK(rdp_graph_destroy(&g)); OK(rdp_graph_init(&g, 1, 1, RDP_MAX_PATH));
    acquire(&g, &t, 1, RDP_NORMAL);
    CHECK(rdp_analyze_lock_request(&g, &t, 2, RDP_NORMAL, &a) == RDP_NODE_LIMIT && !a.complete);
    OK(rdp_record_lock_acquired(&t, 2, EBUSY));
    CHECK(rdp_analyze_lock_request(&g, &t, 3, RDP_NORMAL, &a) == RDP_INCOMPLETE && !a.complete);
    OK(rdp_record_lock_acquired(&t, 3, EBUSY));
    OK(rdp_graph_stats(&g, &stats)); CHECK(!stats.complete);
    OK(rdp_tracker_destroy(&t)); OK(rdp_tracker_destroy(&u)); OK(rdp_graph_destroy(&g));
    puts("PASS analyzer: banking event sequences, pre-request edges, failed calls, recursion, cycle diagnostics");
    return 0;
}
