#include "rdp/tracker.h"
#include <string.h>

static RdpStatus enter(RdpTracker *t)
{
    if (!t) return RDP_INVALID;
    if (!t->initialized) return RDP_UNINITIALIZED;
    return pthread_mutex_lock(&t->mutex) == 0 ? RDP_OK : RDP_SYNC_ERROR;
}
static RdpStatus leave(RdpTracker *t, RdpStatus status)
{
    return pthread_mutex_unlock(&t->mutex) == 0 ? status : RDP_SYNC_ERROR;
}
static size_t find(const RdpTracker *t, RdpLockId lock)
{
    for (size_t i = 0; i < t->count; ++i)
        if (t->held[i].lock == lock) return i;
    return RDP_MAX_HELD;
}
static void snapshot(const RdpTracker *t, RdpSnapshot *s)
{
    memset(s, 0, sizeof(*s));
    s->thread = t->thread; s->count = t->count; s->pending = t->pending;
    s->complete = t->complete;
    memcpy(s->held, t->held, t->count * sizeof(t->held[0]));
}
RdpStatus rdp_tracker_init(RdpTracker *t, RdpThreadId thread, size_t limit, uint32_t depth)
{
    if (!t || !thread || !limit || limit > RDP_MAX_HELD || !depth) return RDP_INVALID;
    if (t->initialized) return RDP_STATE;
    memset(t, 0, sizeof(*t));
    if (pthread_mutex_init(&t->mutex, NULL) != 0) return RDP_SYNC_ERROR;
    t->thread = thread; t->held_limit = limit; t->depth_limit = depth;
    t->complete = true; t->initialized = true;
    return RDP_OK;
}
RdpStatus rdp_tracker_destroy(RdpTracker *t)
{
    if (!t) return RDP_INVALID;
    if (!t->initialized) return RDP_UNINITIALIZED;
    if (pthread_mutex_destroy(&t->mutex) != 0) return RDP_SYNC_ERROR;
    t->initialized = false;
    return RDP_OK;
}
RdpStatus rdp_tracker_snapshot(RdpTracker *t, RdpSnapshot *s)
{
    if (!s) return RDP_INVALID;
    memset(s, 0, sizeof(*s));
    RdpStatus status = enter(t);
    if (status != RDP_OK) return status;
    snapshot(t, s);
    return leave(t, t->complete ? RDP_OK : RDP_INCOMPLETE);
}
RdpStatus rdp_tracker_request(RdpTracker *t, RdpLockId lock, RdpLockKind kind,
                              RdpSnapshot *s)
{
    if (!s || !lock || kind < RDP_NORMAL || kind > RDP_UNKNOWN) return RDP_INVALID;
    memset(s, 0, sizeof(*s));
    RdpStatus status = enter(t);
    if (status != RDP_OK) return status;
    if (!t->complete) return leave(t, RDP_INCOMPLETE);
    if (t->pending) return leave(t, RDP_BUSY);
    size_t i = find(t, lock);
    if (i != RDP_MAX_HELD && t->held[i].kind != kind) return leave(t, RDP_STATE);
    t->pending = lock; t->pending_kind = kind;
    snapshot(t, s);
    return leave(t, RDP_OK);
}
RdpStatus rdp_tracker_acquired(RdpTracker *t, RdpLockId lock, int result)
{
    if (!lock || result < 0) return RDP_INVALID;
    RdpStatus status = enter(t);
    if (status != RDP_OK) return status;
    if (t->pending != lock) return leave(t, RDP_STATE);
    t->pending = 0;
    if (result != 0) return leave(t, RDP_OK); /* Failed calls never add a held lock. */
    size_t i = find(t, lock);
    if (i != RDP_MAX_HELD) {
        if (t->pending_kind != RDP_RECURSIVE) status = RDP_STATE;
        else if (t->held[i].depth == t->depth_limit) status = RDP_DEPTH_LIMIT;
        else ++t->held[i].depth;
    } else if (t->count == t->held_limit) status = RDP_HELD_LIMIT;
    else t->held[t->count++] = (RdpHeldLock){lock, t->pending_kind, 1};
    /* Actual success that cannot be represented makes future analysis incomplete. */
    if (status != RDP_OK) t->complete = false;
    return leave(t, status);
}
RdpStatus rdp_tracker_released(RdpTracker *t, RdpLockId lock, int result)
{
    if (!lock || result < 0) return RDP_INVALID;
    RdpStatus status = enter(t);
    if (status != RDP_OK) return status;
    size_t i = find(t, lock);
    if (i == RDP_MAX_HELD) return leave(t, RDP_NOT_FOUND);
    if (result == 0 && --t->held[i].depth == 0) {
        t->held[i] = t->held[--t->count];
        memset(&t->held[t->count], 0, sizeof(t->held[0]));
    }
    return leave(t, t->complete ? RDP_OK : RDP_INCOMPLETE);
}
