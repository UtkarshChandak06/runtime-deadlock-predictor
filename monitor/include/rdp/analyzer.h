#ifndef RDP_ANALYZER_H
#define RDP_ANALYZER_H
#include "graph.h"
#include "tracker.h"
typedef struct {
    RdpWarning warnings[RDP_MAX_HELD];
    size_t warning_count;
    bool complete;
} RdpAnalysis;
/* No callbacks or I/O under internal locks. Call before the application lock.
 * If a capacity error occurs after request acceptance, still record its actual
 * acquisition result. A result.complete=false is never a clean safety result. */
RdpStatus rdp_analyze_lock_request(RdpGraph *, RdpTracker *, RdpLockId,
                                  RdpLockKind, RdpAnalysis *);
RdpStatus rdp_record_lock_acquired(RdpTracker *, RdpLockId, int pthread_result);
RdpStatus rdp_record_lock_released(RdpTracker *, RdpLockId, int pthread_result);
#endif
