#ifndef RDP_EVENTS_H
#define RDP_EVENTS_H
#include "analyzer.h"
#include <stdint.h>
typedef struct {
    const char *type;              /* Only internal constant strings, never user input. */
    const char *reason;
    const char *kind;
    uint64_t thread_instance;
    RdpLockId lock;
    uintptr_t address;
    int return_code;
    bool has_result;
    bool complete;
    const RdpSnapshot *snapshot;
    const RdpWarning *warning;
} RdpEvent;
/* Internal API: caller holds the TLS reentrancy guard and disables cancellation. */
bool rdp_events_init(void); /* false means invalid sink configuration; fallback used */
bool rdp_event_emit(const RdpEvent *);
bool rdp_events_healthy(void);
#endif
