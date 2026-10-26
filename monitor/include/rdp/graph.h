#ifndef RDP_GRAPH_H
#define RDP_GRAPH_H
#include "types.h"
#include <pthread.h>

/* Stack/static allocation is supported. Zero-initialize before init; never copy
 * a live object. Fields are private-by-contract: use APIs after initialization.
 * Init/destroy require quiescent callers. Reset starts a new historical session. */
typedef struct {
    bool initialized, complete;
    pthread_mutex_t mutex;
    size_t node_limit, edge_limit, path_limit, node_count, edge_count;
    uint64_t sequence;
    RdpLockId nodes[RDP_MAX_NODES];
    bool edges[RDP_MAX_NODES][RDP_MAX_NODES];
} RdpGraph;
typedef struct {
    size_t nodes, edges;
    uint64_t sequence;
    bool complete;
} RdpGraphStats;
RdpStatus rdp_graph_init(RdpGraph *, size_t nodes, size_t edges, size_t path);
RdpStatus rdp_graph_destroy(RdpGraph *);
RdpStatus rdp_graph_reset(RdpGraph *);
RdpStatus rdp_graph_register(RdpGraph *, RdpLockId);
RdpStatus rdp_graph_add(RdpGraph *, RdpThreadId, RdpLockId held,
                        RdpLockId requested, RdpWarning *);
RdpStatus rdp_graph_has_edge(RdpGraph *, RdpLockId, RdpLockId, bool *);
RdpStatus rdp_graph_stats(RdpGraph *, RdpGraphStats *);
#endif
