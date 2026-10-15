#include "rdp/graph.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
static char tokens[RDP_GRAPH_MAX_NODES + 2];
static void *race(void *arg)
{
    size_t i = *(size_t *)arg;
    int r = rdp_graph_cycle_for_edge(&tokens[i], &tokens[(i + 1) % 3], NULL, NULL, NULL, NULL);
    return (void *)(long)r;
}
int main(void)
{
    void *path[RDP_GRAPH_MAX_CYC];
    size_t len;
    rdp_graph_cleanup();
    for (size_t i = 0; i + 1 < RDP_GRAPH_MAX_NODES; ++i)
        assert(rdp_graph_add_edge(&tokens[i], &tokens[i+1]) == RDP_GRAPH_ADDED);
    rdp_graph_init();
    assert(rdp_graph_edge_count() == RDP_GRAPH_MAX_NODES - 1);
    assert(rdp_graph_cycle_for_edge(&tokens[RDP_GRAPH_MAX_NODES-1], &tokens[0], path, &len, NULL, NULL) == RDP_GRAPH_CYCLE_FOUND);
    assert(len == RDP_GRAPH_MAX_NODES);
    for (size_t i = 0; i < len; ++i) assert(path[i] == &tokens[i]);
    assert(rdp_graph_add_edge(&tokens[0], &tokens[RDP_GRAPH_MAX_NODES]) == RDP_GRAPH_ANALYSIS_INCOMPLETE);
    assert(rdp_graph_cycle_for_edge(&tokens[0], &tokens[RDP_GRAPH_MAX_NODES], NULL, NULL, NULL, NULL) == RDP_GRAPH_ANALYSIS_INCOMPLETE);
    assert(rdp_graph_dropped() == 2);
    assert(rdp_graph_node_count() == RDP_GRAPH_MAX_NODES);
    puts("ok - full 1024-node path, idempotent init, node limits in both APIs");

    rdp_graph_cleanup();
    for (size_t i = 0; i < 64; ++i)
        for (size_t j = 0; j < 64; ++j)
            assert(rdp_graph_add_edge(&tokens[i], &tokens[j]) == RDP_GRAPH_ADDED);
    assert(rdp_graph_edge_count() == RDP_GRAPH_MAX_EDGES);
    assert(rdp_graph_add_edge(&tokens[0], &tokens[64]) == RDP_GRAPH_ANALYSIS_INCOMPLETE);
    assert(rdp_graph_cycle_for_edge(&tokens[0], &tokens[64], NULL, NULL, NULL, NULL) == RDP_GRAPH_ANALYSIS_INCOMPLETE);
    assert(rdp_graph_node_count() == 64);
    assert(rdp_graph_add_edge(&tokens[0], &tokens[1]) == RDP_GRAPH_DUPLICATE);
    assert(rdp_graph_cycle_for_edge(&tokens[0], &tokens[1], path, NULL, NULL, NULL) == RDP_GRAPH_CYCLE_FOUND);
    assert(rdp_graph_dropped() == 2);
    puts("ok - edge limits, atomic rejection, duplicates at capacity, optional outputs");

    rdp_graph_cleanup();
    assert(rdp_graph_cycle_for_edge(NULL, &tokens[0], NULL, NULL, NULL, NULL) == RDP_GRAPH_ANALYSIS_INCOMPLETE);
    assert(rdp_graph_dropped() == 1);
    assert(rdp_graph_cycle_for_edge(&tokens[0], &tokens[0], path, &len, NULL, NULL) == RDP_GRAPH_CYCLE_FOUND);
    assert(len == 1 && path[0] == &tokens[0]);
    puts("ok - invalid token reports incomplete; self-edge returns real cycle");

    for (int round = 0; round < 100; ++round) {
        pthread_t threads[3];
        size_t ids[3] = {0,1,2};
        int cycles = 0;
        rdp_graph_cleanup();
        for (int i = 0; i < 3; ++i) assert(!pthread_create(&threads[i], NULL, race, &ids[i]));
        for (int i = 0; i < 3; ++i) {
            void *ret;
            assert(!pthread_join(threads[i], &ret));
            int r = (int)(long)ret;
            assert(r == RDP_GRAPH_CYCLE_FOUND || r == RDP_GRAPH_NO_CYCLE);
            cycles += r == RDP_GRAPH_CYCLE_FOUND;
        }
        assert(cycles == 1 && rdp_graph_edge_count() == 3);
    }
    puts("ok - concurrent closing edges: exactly one detected cycle in 100 rounds");
    puts("TEST graph_limits PASS");
    return 0;
}
