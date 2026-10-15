#include "rdp/graph.h"
#include "rdp/internal.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static void *nodes[RDP_GRAPH_MAX_NODES];
static unsigned char cell[RDP_GRAPH_MAX_NODES][RDP_GRAPH_MAX_NODES];
static size_t nnode, nedge, dropped;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/* Guard even direct graph API calls when linked into the preload library.
 * Unit builds use ordinary pthread calls. No logging or allocation while
 * the graph lock is held. The wrapper releases this lock before blocking
 * on an application mutex. */
static int enter(void)
{
    int previous = 0;
#ifdef RDP_INTERPOSE
    previous = rdp_monitor_enter();
    if (previous == -1)
        return -1;
#endif
    pthread_mutex_lock(&mutex);
    return previous;
}
static void leave(int previous)
{
    if (previous == -1)
        return;
    pthread_mutex_unlock(&mutex);
#ifdef RDP_INTERPOSE
    rdp_monitor_leave(previous);
#else
    (void)previous;
#endif
}
static size_t find(void *token)
{
    for (size_t i = 0; i < nnode; ++i)
        if (nodes[i] == token) return i;
    return RDP_GRAPH_MAX_NODES;
}
static int incomplete(void)
{
    if (dropped != SIZE_MAX) ++dropped;
    return RDP_GRAPH_ANALYSIS_INCOMPLETE;
}
/* All helpers below run with the graph mutex held. */
static size_t reach(size_t start, size_t goal, void **path)
{
    size_t parent[RDP_GRAPH_MAX_NODES], queue[RDP_GRAPH_MAX_NODES];
    size_t head = 0, tail = 0, len = 0;
    for (size_t i = 0; i < nnode; ++i) parent[i] = RDP_GRAPH_MAX_NODES;
    parent[start] = start;
    queue[tail++] = start;
    while (head < tail && parent[goal] == RDP_GRAPH_MAX_NODES) {
        size_t cur = queue[head++];
        for (size_t j = 0; j < nnode; ++j) {
            if (cell[cur][j] && parent[j] == RDP_GRAPH_MAX_NODES) {
                parent[j] = cur;
                queue[tail++] = j;
            }
        }
    }
    if (parent[goal] == RDP_GRAPH_MAX_NODES) return 0;
    for (size_t i = goal;; i = parent[i]) {
        queue[len++] = i;
        if (i == start) break;
    }
    if (path)
        for (size_t i = 0; i < len; ++i) path[i] = nodes[queue[len - i - 1]];
    return len;
}
static int update(void *from, void *to, int analyze, void **path,
                  size_t *plen, void **efrom, void **eto)
{
    int previous = enter(), result;
    if (previous == -1) {
        if (plen) *plen = 0;
        if (efrom) *efrom = NULL;
        if (eto) *eto = NULL;
        return RDP_GRAPH_ANALYSIS_INCOMPLETE;
    }
    size_t fi = find(from), ti = find(to), len = 0;
    if (plen) *plen = 0;
    if (efrom) *efrom = NULL;
    if (eto) *eto = NULL;
    if (!from || !to) {
        result = incomplete();
        goto out;
    }
    size_t missing = (fi == RDP_GRAPH_MAX_NODES) +
                     (ti == RDP_GRAPH_MAX_NODES && from != to);
    if (nnode + missing > RDP_GRAPH_MAX_NODES ||
        (nedge == RDP_GRAPH_MAX_EDGES &&
         (missing || !cell[fi][ti]))) {
        result = incomplete();
        goto out;
    }
    if (fi == RDP_GRAPH_MAX_NODES) { fi = nnode; nodes[nnode++] = from; }
    if (from == to) ti = fi;
    else if (ti == RDP_GRAPH_MAX_NODES) { ti = nnode; nodes[nnode++] = to; }
    if (analyze && cell[fi][ti] == 2) {
        result = RDP_GRAPH_ALREADY_REPORTED;
        goto out;
    }
    if (analyze) len = reach(ti, fi, path);
    result = cell[fi][ti] ? RDP_GRAPH_DUPLICATE : RDP_GRAPH_ADDED;
    if (!cell[fi][ti]) { cell[fi][ti] = 1; ++nedge; }
    if (analyze) {
        result = len ? RDP_GRAPH_CYCLE_FOUND : RDP_GRAPH_NO_CYCLE;
        if (len) {
            cell[fi][ti] = 2;
            if (plen) *plen = len;
            if (efrom) *efrom = from;
            if (eto) *eto = to;
        }
    }
out:
    leave(previous);
    return result;
}
void rdp_graph_init(void) { /* static initialization; never discard history */ }
int rdp_graph_add_edge(void *from, void *to)
{
    return update(from, to, 0, NULL, NULL, NULL, NULL);
}
int rdp_graph_cycle_for_edge(void *from, void *to, void **path,
                             size_t *plen, void **efrom, void **eto)
{
    return update(from, to, 1, path, plen, efrom, eto);
}
size_t rdp_graph_node_count(void)
{
    int p = enter(); if (p == -1) return 0; size_t n = nnode; leave(p); return n;
}
size_t rdp_graph_edge_count(void)
{
    int p = enter(); if (p == -1) return 0; size_t n = nedge; leave(p); return n;
}
size_t rdp_graph_dropped(void)
{
    int p = enter(); if (p == -1) return SIZE_MAX; size_t n = dropped; leave(p); return n;
}
void rdp_graph_cleanup(void)
{
    int p = enter();
    memset(cell, 0, sizeof cell);
    memset(nodes, 0, sizeof nodes);
    nnode = nedge = dropped = 0;
    leave(p);
}
void rdp_graph_format(char *buf, size_t cap)
{
    int p = enter();
    size_t v = nnode, e = nedge, d = dropped;
    leave(p);
    if (buf && cap) snprintf(buf, cap, "%zu nodes, %zu edges, %zu dropped", v, e, d);
}
