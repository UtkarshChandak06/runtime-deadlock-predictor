#include "rdp/graph.h"
#include <string.h>

static RdpStatus enter(RdpGraph *g)
{
    if (!g) return RDP_INVALID;
    if (!g->initialized) return RDP_UNINITIALIZED;
    return pthread_mutex_lock(&g->mutex) == 0 ? RDP_OK : RDP_SYNC_ERROR;
}
static RdpStatus leave(RdpGraph *g, RdpStatus status)
{
    return pthread_mutex_unlock(&g->mutex) == 0 ? status : RDP_SYNC_ERROR;
}
static size_t find(const RdpGraph *g, RdpLockId id)
{
    for (size_t i = 0; i < g->node_count; ++i)
        if (g->nodes[i] == id) return i;
    return RDP_MAX_NODES;
}
static RdpStatus exhausted(RdpGraph *g, RdpStatus status)
{
    g->complete = false;
    return leave(g, status);
}
RdpStatus rdp_graph_init(RdpGraph *g, size_t nodes, size_t edges, size_t path)
{
    if (!g || !nodes || nodes > RDP_MAX_NODES || !edges || edges > RDP_MAX_EDGES ||
        path < 2 || path > RDP_MAX_PATH) return RDP_INVALID;
    if (g->initialized) return RDP_STATE;
    memset(g, 0, sizeof(*g));
    if (pthread_mutex_init(&g->mutex, NULL) != 0) return RDP_SYNC_ERROR;
    g->node_limit = nodes; g->edge_limit = edges; g->path_limit = path;
    g->complete = true; g->initialized = true;
    return RDP_OK;
}
RdpStatus rdp_graph_destroy(RdpGraph *g)
{
    if (!g) return RDP_INVALID;
    if (!g->initialized) return RDP_UNINITIALIZED;
    if (pthread_mutex_destroy(&g->mutex) != 0) return RDP_SYNC_ERROR;
    g->initialized = false;
    return RDP_OK;
}
RdpStatus rdp_graph_reset(RdpGraph *g)
{
    RdpStatus status = enter(g);
    if (status != RDP_OK) return status;
    memset(g->edges, 0, sizeof(g->edges));
    memset(g->nodes, 0, sizeof(g->nodes));
    g->node_count = g->edge_count = 0; g->sequence = 0; g->complete = true;
    return leave(g, RDP_OK);
}
RdpStatus rdp_graph_register(RdpGraph *g, RdpLockId id)
{
    if (!id) return RDP_INVALID;
    RdpStatus status = enter(g);
    if (status != RDP_OK) return status;
    if (!g->complete) return leave(g, RDP_INCOMPLETE);
    if (find(g, id) != RDP_MAX_NODES) return leave(g, RDP_DUPLICATE);
    if (g->node_count == g->node_limit) return exhausted(g, RDP_NODE_LIMIT);
    g->nodes[g->node_count++] = id;
    return leave(g, RDP_OK);
}

/* BFS visits each node once. Parent links recover the shortest existing path.
 * All searches and mutations are in the same graph critical section. */
RdpStatus rdp_graph_add(RdpGraph *g, RdpThreadId thread, RdpLockId held,
                        RdpLockId requested, RdpWarning *warning)
{
    if (warning) memset(warning, 0, sizeof(*warning));
    if (!warning || !held || !requested || !thread) return RDP_INVALID;
    RdpStatus status = enter(g);
    if (status != RDP_OK) return status;
    if (!g->complete) return leave(g, RDP_INCOMPLETE);
    size_t h = find(g, held), l = find(g, requested);
    if (h != RDP_MAX_NODES && l != RDP_MAX_NODES && g->edges[h][l])
        return leave(g, RDP_DUPLICATE);
    size_t needed = (h == RDP_MAX_NODES) + (l == RDP_MAX_NODES && held != requested);
    if (needed > g->node_limit - g->node_count) return exhausted(g, RDP_NODE_LIMIT);
    if (g->edge_count == g->edge_limit) return exhausted(g, RDP_EDGE_LIMIT);
    if (g->sequence == UINT64_MAX) return exhausted(g, RDP_SEQUENCE_LIMIT);

    size_t parent[RDP_MAX_NODES], queue[RDP_MAX_NODES];
    bool seen[RDP_MAX_NODES] = {false};
    bool cycle = held == requested;
    if (!cycle && h != RDP_MAX_NODES && l != RDP_MAX_NODES) {
        size_t head = 0, tail = 0;
        queue[tail++] = l; seen[l] = true; parent[l] = l;
        while (head < tail && !seen[h]) {
            size_t u = queue[head++];
            for (size_t v = 0; v < g->node_count; ++v) {
                if (g->edges[u][v] && !seen[v]) {
                    seen[v] = true; parent[v] = u; queue[tail++] = v;
                }
            }
        }
        cycle = seen[h];
    }
    RdpLockId path[RDP_MAX_PATH];
    size_t length = 0;
    if (cycle) {
        if (held == requested) { path[length++] = held; path[length++] = held; }
        else {
            size_t reverse[RDP_MAX_NODES], count = 0;
            for (size_t node = h; ; node = parent[node]) {
                reverse[count++] = node;
                if (node == l) break;
            }
            while (count) path[length++] = g->nodes[reverse[--count]];
            path[length++] = requested;
        }
        if (length > g->path_limit) return exhausted(g, RDP_PATH_LIMIT);
    }
    /* Commit only after all capacity checks; no partial new edge/node insertion. */
    if (h == RDP_MAX_NODES) { h = g->node_count; g->nodes[g->node_count++] = held; }
    if (l == RDP_MAX_NODES) {
        if (held == requested) l = h;
        else { l = g->node_count; g->nodes[g->node_count++] = requested; }
    }
    g->edges[h][l] = true; ++g->edge_count; ++g->sequence;
    if (cycle) {
        warning->type = RDP_POTENTIAL_LOCK_ORDER_CYCLE;
        warning->thread = thread; warning->held_lock = held;
        warning->requested_lock = requested; warning->sequence = g->sequence;
        warning->cycle_length = length;
        memcpy(warning->cycle, path, length * sizeof(*path));
    }
    return leave(g, RDP_OK);
}
RdpStatus rdp_graph_has_edge(RdpGraph *g, RdpLockId from, RdpLockId to, bool *found)
{
    if (!found || !from || !to) return RDP_INVALID;
    *found = false;
    RdpStatus status = enter(g);
    if (status != RDP_OK) return status;
    size_t a = find(g, from), b = find(g, to);
    *found = a != RDP_MAX_NODES && b != RDP_MAX_NODES && g->edges[a][b];
    return leave(g, g->complete ? RDP_OK : RDP_INCOMPLETE);
}
RdpStatus rdp_graph_stats(RdpGraph *g, RdpGraphStats *stats)
{
    if (!stats) return RDP_INVALID;
    RdpStatus status = enter(g);
    if (status != RDP_OK) return status;
    *stats = (RdpGraphStats){g->node_count, g->edge_count, g->sequence, g->complete};
    return leave(g, RDP_OK);
}
