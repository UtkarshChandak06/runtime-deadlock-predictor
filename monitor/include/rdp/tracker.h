#ifndef RDP_TRACKER_H
#define RDP_TRACKER_H
#include "types.h"
#include <pthread.h>

typedef struct { RdpLockId lock; RdpLockKind kind; uint32_t depth; } RdpHeldLock;
typedef struct {
    RdpThreadId thread;
    RdpHeldLock held[RDP_MAX_HELD];
    size_t count;
    RdpLockId pending;
    bool complete;
} RdpSnapshot;
/* Zero-initialize; do not copy live objects. One logical thread per tracker.
 * Operations are synchronized, but callers must preserve logical event order.
 * Lifecycle operations require no concurrent callers. */
typedef struct {
    bool initialized, complete;
    pthread_mutex_t mutex;
    RdpThreadId thread;
    size_t count, held_limit;
    uint32_t depth_limit;
    RdpHeldLock held[RDP_MAX_HELD];
    RdpLockId pending;
    RdpLockKind pending_kind;
} RdpTracker;
RdpStatus rdp_tracker_init(RdpTracker *, RdpThreadId, size_t held_limit, uint32_t depth_limit);
RdpStatus rdp_tracker_destroy(RdpTracker *);
RdpStatus rdp_tracker_snapshot(RdpTracker *, RdpSnapshot *);
/* Request saves pending metadata and returns the pre-acquisition held set. */
RdpStatus rdp_tracker_request(RdpTracker *, RdpLockId, RdpLockKind, RdpSnapshot *);
/* pthread_result == 0 means success; nonzero means failure, not acquisition. */
RdpStatus rdp_tracker_acquired(RdpTracker *, RdpLockId, int pthread_result);
RdpStatus rdp_tracker_released(RdpTracker *, RdpLockId, int pthread_result);
#endif
