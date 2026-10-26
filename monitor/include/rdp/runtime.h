#ifndef RDP_RUNTIME_H
#define RDP_RUNTIME_H
#include "events.h"
#include <pthread.h>
extern _Thread_local unsigned rdp_runtime_guard;
typedef struct {
    RdpLockId id;
    uintptr_t address;
    RdpLockKind kind;
    const char *kind_name;
    bool pending;
} RdpRuntimeCall;
bool rdp_runtime_ready(void);
void rdp_runtime_before_lock(pthread_mutex_t *, RdpRuntimeCall *);
void rdp_runtime_after_lock(const RdpRuntimeCall *, int);
void rdp_runtime_before_unlock(pthread_mutex_t *, RdpRuntimeCall *);
void rdp_runtime_after_unlock(const RdpRuntimeCall *, int);
void rdp_runtime_after_init(pthread_mutex_t *, const pthread_mutexattr_t *, int);
void rdp_runtime_after_destroy(pthread_mutex_t *, int);
void rdp_runtime_finish(void);
#endif
