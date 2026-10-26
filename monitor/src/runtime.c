#define _GNU_SOURCE
#include "rdp/runtime.h"
#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#define RDP_RUNTIME_LOCKS 64u
_Thread_local unsigned rdp_runtime_guard;
static RdpGraph graph;
static pthread_once_t initialization = PTHREAD_ONCE_INIT;
static pthread_mutex_t registry_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_key_t thread_key;
static bool enabled, key_ready;
static pid_t owner_pid;
static atomic_bool complete = true;
static atomic_uint_fast64_t next_thread = 1;
typedef struct {
    uintptr_t address;
    RdpLockId id;
    RdpLockKind kind;
    const char *name;
    bool active;
} Identity;
static Identity identities[RDP_RUNTIME_LOCKS];
static RdpLockId next_lock = 1;
typedef struct { RdpTracker tracker; uint64_t instance; bool ready; } ThreadState;
static _Thread_local ThreadState local;

static void emit(RdpEvent *e)
{
    e->thread_instance = local.instance;
    e->complete = atomic_load(&complete);
    if (!rdp_event_emit(e)) atomic_store(&complete, false);
}
static void incomplete(const char *reason, int code)
{
    bool previous = atomic_exchange(&complete, false);
    if (previous) {
        RdpEvent e = {.type = "monitoring_incomplete", .reason = reason, .return_code = code};
        emit(&e);
    }
}
static const char *status_reason(RdpStatus status)
{
    switch (status) {
    case RDP_NODE_LIMIT: return "graph_node_capacity";
    case RDP_EDGE_LIMIT: return "graph_edge_capacity";
    case RDP_PATH_LIMIT: return "cycle_path_capacity";
    case RDP_HELD_LIMIT: return "held_lock_capacity";
    case RDP_DEPTH_LIMIT: return "recursive_depth_capacity";
    case RDP_NOT_FOUND: return "successful_unlock_not_tracked";
    case RDP_STATE: return "inconsistent_or_unknown_mutex_semantics";
    default: return "analysis_state_error";
    }
}
static void thread_end(void *value)
{
    ThreadState *state = value;
    if (getpid() != owner_pid || !state->ready) return;
    ++rdp_runtime_guard;
    RdpSnapshot snapshot;
    RdpStatus status = rdp_tracker_snapshot(&state->tracker, &snapshot);
    if (status != RDP_OK || snapshot.count || snapshot.pending)
        incomplete("thread_exit_with_incomplete_or_held_state", status);
    RdpEvent e = {.type = "thread_end", .snapshot = &snapshot}; emit(&e);
    status = rdp_tracker_destroy(&state->tracker);
    if (status != RDP_OK) incomplete("tracker_cleanup_failed", status);
    state->ready = false;
    --rdp_runtime_guard;
}
static void initialize(void)
{
    owner_pid = getpid();
    const char *setting = getenv("RDP_ENABLED");
    if (setting && strcmp(setting, "0") == 0) return;
    bool valid_fd = rdp_events_init();
    if (setting && strcmp(setting, "1") != 0) {
        incomplete("invalid_RDP_ENABLED_monitor_disabled", RDP_INVALID);
        return;
    }
    RdpStatus status = rdp_graph_init(&graph, RDP_MAX_NODES, RDP_MAX_EDGES, RDP_MAX_PATH);
    if (status != RDP_OK) { incomplete("graph_init_failed", status); return; }
    int result = pthread_key_create(&thread_key, thread_end);
    if (result != 0) { incomplete("thread_key_init_failed", result); return; }
    key_ready = true; enabled = true;
    if (!valid_fd) incomplete("invalid_event_fd_using_stderr", RDP_INVALID);
    RdpEvent e = {.type = "session_start", .reason = "explicit_init_types_static_types_unknown"}; emit(&e);
}
bool rdp_runtime_ready(void)
{
    if (pthread_once(&initialization, initialize) != 0) return false;
    /* After fork, do not touch potentially inherited locked monitor mutexes.
     * Only exec starts a fresh supported session in the child. */
    return enabled && getpid() == owner_pid;
}
static bool thread_ready(void)
{
    if (local.ready) return true;
    RdpStatus status = rdp_tracker_init(&local.tracker, (uint64_t)syscall(SYS_gettid),
                                       RDP_MAX_HELD, UINT32_MAX);
    if (status != RDP_OK) { incomplete("tracker_init_failed", status); return false; }
    local.instance = atomic_fetch_add(&next_thread, 1);
    int result = key_ready ? pthread_setspecific(thread_key, &local) : EINVAL;
    if (result != 0 || local.instance == 0) {
        incomplete("thread_state_registration_failed", result);
        (void)rdp_tracker_destroy(&local.tracker); return false;
    }
    local.ready = true;
    RdpEvent e = {.type = "thread_start"}; emit(&e);
    return true;
}

/* uintptr_t preserves the entire address. IDs are monotonic generations, never
 * recycled. Successful destroy retires a mapping, but keeps historical edges. */
static bool identity(pthread_mutex_t *mutex, RdpRuntimeCall *call, bool initializing,
                     RdpLockKind kind, const char *name)
{
    memset(call, 0, sizeof(*call)); call->address = (uintptr_t)mutex;
    int error = pthread_mutex_lock(&registry_mutex);
    if (error != 0) { incomplete("identity_mutex_failed", error); return false; }
    size_t slot = RDP_RUNTIME_LOCKS, free_slot = RDP_RUNTIME_LOCKS;
    for (size_t i = 0; i < RDP_RUNTIME_LOCKS; ++i) {
        if (identities[i].active && identities[i].address == call->address) slot = i;
        if (!identities[i].active && free_slot == RDP_RUNTIME_LOCKS) free_slot = i;
    }
    bool created = slot == RDP_RUNTIME_LOCKS;
    if (created) slot = free_slot;
    bool valid = slot != RDP_RUNTIME_LOCKS && next_lock != UINT64_MAX;
    if (valid) {
        Identity *entry = &identities[slot];
        if (created || initializing) {
            *entry = (Identity){call->address, next_lock++, initializing ? kind : RDP_UNKNOWN,
                               initializing ? name : "unknown", true};
        }
        call->id = entry->id; call->kind = entry->kind; call->kind_name = entry->name;
    }
    error = pthread_mutex_unlock(&registry_mutex);
    if (error != 0) { incomplete("identity_mutex_failed", error); return false; }
    if (!valid) { incomplete("identity_capacity", RDP_NODE_LIMIT); return false; }
    if (created && !initializing) {
        RdpEvent e = {.type = "mutex_kind_unknown", .lock = call->id,
                      .address = call->address, .kind = "unknown",
                      .reason = "initialization_not_observed_conservative_self_lock_policy"}; emit(&e);
    }
    return true;
}
static void operation_event(const char *type, const RdpRuntimeCall *call, int result)
{
    RdpSnapshot snapshot = {0};
    if (local.ready) (void)rdp_tracker_snapshot(&local.tracker, &snapshot);
    RdpEvent e = {.type = type, .lock = call->id, .address = call->address,
                  .kind = call->kind_name, .return_code = result,
                  .has_result = strcmp(type, "lock_request") != 0,
                  .snapshot = local.ready ? &snapshot : NULL};
    emit(&e);
}
void rdp_runtime_before_lock(pthread_mutex_t *mutex, RdpRuntimeCall *call)
{
    bool mapped = identity(mutex, call, false, RDP_UNKNOWN, "unknown");
    bool ready = thread_ready();
    operation_event("lock_request", call, 0);
    if (!mapped || !ready || !atomic_load(&complete)) return;
    RdpAnalysis analysis;
    RdpStatus status = rdp_analyze_lock_request(&graph, &local.tracker, call->id, call->kind, &analysis);
    RdpSnapshot snapshot;
    (void)rdp_tracker_snapshot(&local.tracker, &snapshot);
    call->pending = snapshot.pending == call->id;
    if (status != RDP_OK) incomplete(status_reason(status), status);
    for (size_t i = 0; i < analysis.warning_count; ++i) {
        RdpEvent e = {.type = "potential_lock_order_cycle", .warning = &analysis.warnings[i]}; emit(&e);
    }
}
void rdp_runtime_after_lock(const RdpRuntimeCall *call, int result)
{
    /* EOWNERDEAD grants ownership despite a nonzero return. Robust recovery is
     * outside this model: never mislabel it as an ordinary failed acquisition. */
    if (result == EOWNERDEAD) incomplete("robust_owner_dead_unsupported", result);
    if (call->pending) {
        RdpStatus status = rdp_record_lock_acquired(&local.tracker, call->id, result);
        if (status != RDP_OK) incomplete(status_reason(status), status);
    }
    operation_event(result == 0 ? "lock_acquired" : result == EOWNERDEAD ?
                    "lock_owner_dead" : "lock_failed", call, result);
}
void rdp_runtime_before_unlock(pthread_mutex_t *mutex, RdpRuntimeCall *call)
{
    (void)identity(mutex, call, false, RDP_UNKNOWN, "unknown");
    (void)thread_ready();
}
void rdp_runtime_after_unlock(const RdpRuntimeCall *call, int result)
{
    if (result == 0 && local.ready && call->id && atomic_load(&complete)) {
        RdpStatus status = rdp_record_lock_released(&local.tracker, call->id, result);
        if (status != RDP_OK) incomplete(status_reason(status), status);
    }
    operation_event(result == 0 ? "lock_released" : "unlock_failed", call, result);
}
void rdp_runtime_after_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr, int result)
{
    if (result != 0) {
        RdpEvent e = {.type = "mutex_init_failed", .address = (uintptr_t)mutex,
                      .return_code = result, .has_result = true}; emit(&e);
        return;
    }
    int type = PTHREAD_MUTEX_NORMAL, robust = PTHREAD_MUTEX_STALLED, shared = PTHREAD_PROCESS_PRIVATE;
    if (attr && (pthread_mutexattr_gettype(attr, &type) != 0 ||
                 pthread_mutexattr_getrobust(attr, &robust) != 0 ||
                 pthread_mutexattr_getpshared(attr, &shared) != 0)) {
        incomplete("mutex_attribute_query_failed", RDP_STATE); return;
    }
    RdpLockKind kind = type == PTHREAD_MUTEX_RECURSIVE ? RDP_RECURSIVE : RDP_NORMAL;
    const char *name = type == PTHREAD_MUTEX_RECURSIVE ? "recursive" :
                       type == PTHREAD_MUTEX_ERRORCHECK ? "errorcheck" : "normal";
    RdpRuntimeCall call;
    if (!identity(mutex, &call, true, kind, name)) return;
    operation_event("mutex_init", &call, result);
    if (robust != PTHREAD_MUTEX_STALLED || shared != PTHREAD_PROCESS_PRIVATE)
        incomplete("robust_or_interprocess_mutex_unsupported", RDP_STATE);
}
void rdp_runtime_after_destroy(pthread_mutex_t *mutex, int result)
{
    RdpRuntimeCall call = {.address = (uintptr_t)mutex};
    int error = pthread_mutex_lock(&registry_mutex);
    if (error != 0) { incomplete("identity_mutex_failed", error); return; }
    for (size_t i = 0; i < RDP_RUNTIME_LOCKS; ++i) {
        Identity *entry = &identities[i];
        if (entry->active && entry->address == call.address) {
            call.id = entry->id; call.kind_name = entry->name;
            if (result == 0) entry->active = false;
            break;
        }
    }
    error = pthread_mutex_unlock(&registry_mutex);
    if (error != 0) incomplete("identity_mutex_failed", error);
    operation_event(result == 0 ? "mutex_destroy" : "mutex_destroy_failed", &call, result);
}
void rdp_runtime_finish(void)
{
    if (enabled && getpid() == owner_pid) {
        RdpEvent e = {.type = "session_end"}; emit(&e);
    }
    /* Process-lifetime global locks/FD are reclaimed by the OS. Do not destroy
     * them while other threads may still exist during exit(). */
}
