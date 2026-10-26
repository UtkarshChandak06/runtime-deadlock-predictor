#ifndef ANALYSIS_TEST_H
#define ANALYSIS_TEST_H
#include "rdp/analyzer.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define OK(x) CHECK((x) == RDP_OK)
static inline void graph_init(RdpGraph *g)
{ OK(rdp_graph_init(g, RDP_MAX_NODES, RDP_MAX_EDGES, RDP_MAX_PATH)); }
static inline void tracker_init(RdpTracker *t, RdpThreadId id)
{ OK(rdp_tracker_init(t, id, RDP_MAX_HELD, UINT32_MAX)); }
static inline RdpWarning edge(RdpGraph *g, RdpLockId a, RdpLockId b)
{ RdpWarning w; OK(rdp_graph_add(g, 77, a, b, &w)); return w; }
static inline void cycle_is(const RdpWarning *w, const RdpLockId *path, size_t count)
{
    CHECK(w->type == RDP_POTENTIAL_LOCK_ORDER_CYCLE && w->cycle_length == count);
    for (size_t i = 0; i < count; ++i) CHECK(w->cycle[i] == path[i]);
    CHECK(w->held_lock == path[count - 2] && w->requested_lock == path[0]);
}
static inline RdpAnalysis request(RdpGraph *g, RdpTracker *t, RdpLockId id, RdpLockKind kind)
{ RdpAnalysis a; OK(rdp_analyze_lock_request(g, t, id, kind, &a)); CHECK(a.complete); return a; }
static inline void acquire(RdpGraph *g, RdpTracker *t, RdpLockId id, RdpLockKind kind)
{ (void)request(g, t, id, kind); OK(rdp_record_lock_acquired(t, id, 0)); }
static inline void tracked_acquire(RdpTracker *t, RdpLockId id, RdpLockKind kind)
{ RdpSnapshot s; OK(rdp_tracker_request(t, id, kind, &s)); OK(rdp_tracker_acquired(t, id, 0)); }
#endif
