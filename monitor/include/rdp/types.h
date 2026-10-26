#ifndef RDP_TYPES_H
#define RDP_TYPES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RDP_MAX_NODES 32u
#define RDP_MAX_EDGES 128u
#define RDP_MAX_HELD 16u
#define RDP_MAX_PATH (RDP_MAX_NODES + 1u)
typedef uint64_t RdpLockId; /* Opaque, caller-assigned, nonzero identity. */
typedef uint64_t RdpThreadId;
typedef enum {
    RDP_OK, RDP_DUPLICATE, RDP_INVALID, RDP_UNINITIALIZED, RDP_BUSY,
    RDP_NOT_FOUND, RDP_STATE, RDP_NODE_LIMIT, RDP_EDGE_LIMIT,
    RDP_PATH_LIMIT, RDP_HELD_LIMIT, RDP_DEPTH_LIMIT, RDP_SEQUENCE_LIMIT,
    RDP_INCOMPLETE, RDP_SYNC_ERROR
} RdpStatus;
typedef enum { RDP_NORMAL, RDP_RECURSIVE, RDP_UNKNOWN } RdpLockKind;
typedef enum { RDP_NO_WARNING, RDP_POTENTIAL_LOCK_ORDER_CYCLE } RdpWarningType;
typedef struct {
    RdpWarningType type;
    RdpThreadId thread;
    RdpLockId held_lock, requested_lock; /* Also the closing edge: held -> requested. */
    uint64_t sequence; /* New-edge sequence within one graph session. */
    size_t cycle_length;
    RdpLockId cycle[RDP_MAX_PATH]; /* requested -> ... -> held -> requested */
} RdpWarning;
#endif
