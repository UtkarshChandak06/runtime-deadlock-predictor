#include "rdp/analyzer.h"
#include <string.h>

RdpStatus rdp_analyze_lock_request(RdpGraph *g, RdpTracker *t, RdpLockId lock,
                                  RdpLockKind kind, RdpAnalysis *result)
{
    if (!result) return RDP_INVALID;
    memset(result, 0, sizeof(*result));
    if (!g || !t || !lock || kind < RDP_NORMAL || kind > RDP_UNKNOWN) return RDP_INVALID;
    RdpGraphStats stats;
    RdpStatus status = rdp_graph_stats(g, &stats);
    if (status != RDP_OK) return status;
    RdpSnapshot held;
    status = rdp_tracker_request(t, lock, kind, &held);
    if (status != RDP_OK) return status;
    /* Never nest tracker and graph mutexes: snapshot has released tracker mutex.
     * Even a fail-stopped graph accepts the pending tracker event, allowing the
     * caller to record the real operation's result afterwards. */
    status = rdp_graph_register(g, lock);
    if (status != RDP_OK && status != RDP_DUPLICATE) return status;
    bool recursive_reentry = false;
    for (size_t i = 0; i < held.count; ++i)
        if (held.held[i].lock == lock && kind == RDP_RECURSIVE) recursive_reentry = true;
    if (!recursive_reentry) {
        for (size_t i = 0; i < held.count; ++i) {
            RdpWarning warning;
            status = rdp_graph_add(g, held.thread, held.held[i].lock, lock, &warning);
            if (status != RDP_OK && status != RDP_DUPLICATE) return status;
            if (warning.type != RDP_NO_WARNING)
                result->warnings[result->warning_count++] = warning;
        }
    }
    /* Reacquiring an already-owned recursive mutex cannot block on another
     * owner; suppress ALL edges for that reentry, not just its self-edge. */
    status = rdp_graph_stats(g, &stats);
    if (status != RDP_OK) return status;
    result->complete = stats.complete;
    return stats.complete ? RDP_OK : RDP_INCOMPLETE;
}
RdpStatus rdp_record_lock_acquired(RdpTracker *t, RdpLockId lock, int result)
{
    return rdp_tracker_acquired(t, lock, result);
}
RdpStatus rdp_record_lock_released(RdpTracker *t, RdpLockId lock, int result)
{
    return rdp_tracker_released(t, lock, result);
}
