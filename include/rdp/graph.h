#ifndef RDP_GRAPH_H
#define RDP_GRAPH_H
#include <stddef.h>

/* Opaque address identities, never dereferenced. Static storage: about
 * 1 MiB matrix + 8 KiB node table on 64-bit hosts; no runtime allocation.
 * All operations serialized; BFS scans matrix rows in O(V^2) time,
 * with O(V) traversal scratch. Limits apply to BOTH update APIs. */
#define RDP_GRAPH_MAX_NODES 1024
#define RDP_GRAPH_MAX_EDGES 4096
#define RDP_GRAPH_MAX_CYC RDP_GRAPH_MAX_NODES
#define RDP_GRAPH_ADDED 0
#define RDP_GRAPH_DUPLICATE 1
#define RDP_GRAPH_SELF_LOOP 2 /* retained for source compatibility */
#define RDP_GRAPH_ANALYSIS_INCOMPLETE 3
#define RDP_GRAPH_NO_CYCLE 10
#define RDP_GRAPH_CYCLE_FOUND 11
#define RDP_GRAPH_ALREADY_REPORTED 12

/* Static initialization is sufficient; init is an idempotent no-op.
 * cleanup resets all state, intended for isolated tests only. */
void rdp_graph_init(void);
void rdp_graph_cleanup(void);
int rdp_graph_add_edge(void *from, void *to);
/* Atomically search to -> ... -> from BEFORE recording from -> to.
 * If path is non-NULL it must hold RDP_GRAPH_MAX_CYC pointers.
 * All output arguments are optional. The complete path excludes the
 * repeated closing 'to': append that token to display the full cycle.
 * Self-edges are cycles too; recursive mutex types are not intercepted.
 * Repeated reported closing edges return ALREADY_REPORTED.
 * INCOMPLETE means an invalid token or exhausted capacity. dropped()
 * remains nonzero after any such loss, even if later calls succeed. */
int rdp_graph_cycle_for_edge(void *from, void *to, void *path[],
                             size_t *plen, void **efrom, void **eto);
size_t rdp_graph_node_count(void);
size_t rdp_graph_edge_count(void);
size_t rdp_graph_dropped(void);
/* Bounded summary; formatting occurs outside the graph lock. */
void rdp_graph_format(char *buf, size_t cap);
#endif
