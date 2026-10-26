#include "analysis_test.h"

int main(void)
{
    RdpGraph g = {0}; RdpGraphStats stats; RdpWarning w; bool found;
    CHECK(rdp_graph_stats(&g, &stats) == RDP_UNINITIALIZED);
    CHECK(rdp_graph_init(&g, 0, 1, 2) == RDP_INVALID);
    graph_init(&g);
    CHECK(rdp_graph_init(&g, 2, 2, 2) == RDP_STATE);
    OK(rdp_graph_stats(&g, &stats)); CHECK(stats.nodes == 0 && stats.edges == 0 && stats.complete);
    CHECK(rdp_graph_add(&g, 1, 0, 2, &w) == RDP_INVALID);
    CHECK(rdp_graph_add(&g, 0, 1, 2, &w) == RDP_INVALID);
    CHECK(rdp_graph_add(&g, 1, 1, 2, NULL) == RDP_INVALID);
    CHECK(edge(&g, 10, 20).type == RDP_NO_WARNING);
    OK(rdp_graph_has_edge(&g, 10, 20, &found)); CHECK(found);
    OK(rdp_graph_has_edge(&g, 20, 10, &found)); CHECK(!found);
    CHECK(rdp_graph_add(&g, 1, 10, 20, &w) == RDP_DUPLICATE && w.type == RDP_NO_WARNING);
    w = edge(&g, 20, 10);
    cycle_is(&w, (RdpLockId[]){10, 20, 10}, 3);
    CHECK(w.thread == 77 && w.sequence == 2);
    CHECK(rdp_graph_add(&g, 1, 20, 10, &w) == RDP_DUPLICATE && w.type == RDP_NO_WARNING);
    OK(rdp_graph_stats(&g, &stats)); CHECK(stats.edges == 2 && stats.sequence == 2);
    OK(rdp_graph_reset(&g));
    CHECK(edge(&g, 100, 200).type == RDP_NO_WARNING);
    CHECK(edge(&g, 200, 300).type == RDP_NO_WARNING);
    w = edge(&g, 300, 100); cycle_is(&w, (RdpLockId[]){100,200,300,100}, 4);
    OK(rdp_graph_reset(&g));
    /* Longest representable simple cycle exercises every BFS/path slot. */
    for (RdpLockId i = 1; i < RDP_MAX_NODES; ++i) CHECK(edge(&g, i, i + 1).type == RDP_NO_WARNING);
    w = edge(&g, RDP_MAX_NODES, 1);
    CHECK(w.cycle_length == RDP_MAX_PATH);
    for (size_t i = 0; i < RDP_MAX_NODES; ++i) CHECK(w.cycle[i] == i + 1);
    CHECK(w.cycle[RDP_MAX_NODES] == 1);
    OK(rdp_graph_reset(&g));
    /* Diamond: undirected connectivity is NOT a directed cycle. */
    CHECK(edge(&g, 1, 2).type == RDP_NO_WARNING);
    CHECK(edge(&g, 1, 3).type == RDP_NO_WARNING);
    CHECK(edge(&g, 2, 4).type == RDP_NO_WARNING);
    CHECK(edge(&g, 3, 4).type == RDP_NO_WARNING);
    CHECK(edge(&g, 90, 91).type == RDP_NO_WARNING); /* independent component */
    w = edge(&g, 4, 1); cycle_is(&w, (RdpLockId[]){1,2,4,1}, 4);
    CHECK(edge(&g, 92, 90).type == RDP_NO_WARNING);
    OK(rdp_graph_reset(&g));
    (void)edge(&g, 1, 2); (void)edge(&g, 2, 3); (void)edge(&g, 3, 5);
    (void)edge(&g, 1, 4); (void)edge(&g, 4, 5);
    w = edge(&g, 5, 1); cycle_is(&w, (RdpLockId[]){1,4,5,1}, 4); /* shortest BFS route */
    OK(rdp_graph_reset(&g));
    w = edge(&g, UINT64_MAX, UINT64_MAX);
    cycle_is(&w, (RdpLockId[]){UINT64_MAX, UINT64_MAX}, 2);
    OK(rdp_graph_destroy(&g));

    OK(rdp_graph_init(&g, 2, 10, RDP_MAX_PATH));
    (void)edge(&g, 1, 2);
    CHECK(rdp_graph_add(&g, 1, 2, 3, &w) == RDP_NODE_LIMIT);
    OK(rdp_graph_stats(&g, &stats)); CHECK(!stats.complete && stats.nodes == 2 && stats.edges == 1);
    CHECK(rdp_graph_add(&g, 1, 2, 1, &w) == RDP_INCOMPLETE);
    CHECK(rdp_graph_has_edge(&g, 2, 3, &found) == RDP_INCOMPLETE && !found);
    OK(rdp_graph_reset(&g)); OK(rdp_graph_register(&g, 8));
    CHECK(rdp_graph_register(&g, 8) == RDP_DUPLICATE);
    OK(rdp_graph_destroy(&g));
    OK(rdp_graph_init(&g, 4, 1, RDP_MAX_PATH)); (void)edge(&g, 1, 2);
    CHECK(rdp_graph_add(&g, 1, 2, 1, &w) == RDP_EDGE_LIMIT);
    OK(rdp_graph_stats(&g, &stats)); CHECK(!stats.complete && stats.edges == 1);
    OK(rdp_graph_destroy(&g));
    OK(rdp_graph_init(&g, 4, 10, 3)); (void)edge(&g, 1, 2); (void)edge(&g, 2, 3);
    CHECK(rdp_graph_add(&g, 1, 3, 1, &w) == RDP_PATH_LIMIT && w.type == RDP_NO_WARNING);
    OK(rdp_graph_stats(&g, &stats)); CHECK(!stats.complete && stats.edges == 2);
    OK(rdp_graph_destroy(&g));
    CHECK(rdp_graph_reset(&g) == RDP_UNINITIALIZED);
    graph_init(&g);
    size_t inserted = 0;
    for (RdpLockId a = 1; a <= RDP_MAX_NODES && inserted < RDP_MAX_EDGES; ++a)
        for (RdpLockId b = a + 1; b <= RDP_MAX_NODES && inserted < RDP_MAX_EDGES; ++b) {
            CHECK(edge(&g, a, b).type == RDP_NO_WARNING); ++inserted;
        }
    OK(rdp_graph_stats(&g, &stats)); CHECK(stats.edges == RDP_MAX_EDGES && stats.complete);
    CHECK(rdp_graph_add(&g, 1, 31, 32, &w) == RDP_EDGE_LIMIT);
    OK(rdp_graph_destroy(&g));
    puts("PASS graph: directed BFS, full paths, duplicates, reset, identities, capacities");
    return 0;
}
