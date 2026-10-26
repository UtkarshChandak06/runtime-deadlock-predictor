import type {
  EventItem,
  Graph,
  Thread,
  Threads,
  Alerts,
  LockNode,
  Dependency,
  RuntimeEvent,
} from "../api/types";
/** Historical projection only. Warnings are copied from C; no cycle search exists here. */
export function reconstruct(
  items: EventItem[],
  count: number,
  truncated = false,
): { graph: Graph; threads: Threads; alerts: Alerts } {
  const nodes = new Map<string, LockNode>(),
    edges = new Map<string, Dependency>(),
    threads = new Map<string, Thread>(),
    alerts: RuntimeEvent[] = [];
  let reliable = !truncated,
    previous = 0n,
    complete = true,
    sequenceComplete = !truncated;
  for (const { event: e } of items.slice(0, count)) {
    if (BigInt(e.sequence) !== previous + 1n) {
      reliable = false;
      sequenceComplete = false;
    }
    previous = BigInt(e.sequence);
    reliable &&= e.monitoring_complete;
    const locks = new Set([
      ...(e.held_locks || []),
      ...(e.cycle || []),
      ...(e.lock_id ? [e.lock_id] : []),
    ]);
    for (const lock of locks) {
      if (!nodes.has(lock)) {
        if (nodes.size >= 1024) {
          complete = false;
          continue;
        }
        nodes.set(lock, {
          lock_id: lock,
          first_sequence: e.sequence,
          destroy_observed: false,
        });
      }
      if (e.lock_id === lock) {
        const node = nodes.get(lock)!;
        if (e.lock_kind) node.lock_kind = e.lock_kind;
        if (e.mutex_address) node.mutex_address = e.mutex_address;
        if (e.session_event === "mutex_destroy") node.destroy_observed = true;
      }
    }
    if (
      e.session_event === "lock_request" &&
      reliable &&
      e.lock_id &&
      !(e.lock_kind === "recursive" && e.held_locks?.includes(e.lock_id))
    ) {
      for (const source of e.held_locks || []) {
        const key = source + ">" + e.lock_id;
        if (!edges.has(key)) {
          if (edges.size >= 1024) {
            complete = false;
            continue;
          }
          edges.set(key, {
            from_lock: source,
            to_lock: e.lock_id,
            first_sequence: e.sequence,
            last_sequence: e.sequence,
            request_count: 0,
            last_thread_id: e.thread_id,
            evidence: "observed_held_at_request",
          });
        }
        const edge = edges.get(key)!;
        edge.last_sequence = e.sequence;
        edge.request_count++;
        edge.last_thread_id = e.thread_id;
      }
    }
    if (e.session_event === "potential_lock_order_cycle") {
      alerts.push(e);
      if (alerts.length > 1024) {
        alerts.shift();
        complete = false;
      }
    }
    if (BigInt(e.thread_instance) === 0n) continue;
    const key = e.thread_id + ":" + e.thread_instance;
    if (!threads.has(key)) {
      if (threads.size >= 1024) {
        complete = false;
        continue;
      }
      threads.set(key, {
        thread_id: e.thread_id,
        thread_instance: e.thread_instance,
        operation_count: 0,
        last_observed_held_locks: [],
        last_observed_depths: [],
        last_requested_lock: null,
        observation_reliable: reliable && !!e.held_locks,
        exit_observed: false,
        history: [],
        last_event: e.session_event,
        last_sequence: e.sequence,
      });
    }
    const thread = threads.get(key)!;
    thread.last_event = e.session_event;
    thread.last_sequence = e.sequence;
    if (e.held_locks) {
      thread.last_observed_held_locks = e.held_locks;
      thread.last_observed_depths = e.held_depths || [];
      thread.observation_reliable = reliable;
    }
    if (!reliable) thread.observation_reliable = false;
    if (e.session_event === "lock_request")
      thread.last_requested_lock = e.lock_id || null;
    if (
      ["lock_acquired", "lock_failed", "lock_owner_dead"].includes(
        e.session_event,
      )
    )
      thread.last_requested_lock = null;
    if (e.session_event === "thread_end") thread.exit_observed = true;
    if (
      [
        "lock_request",
        "lock_acquired",
        "lock_failed",
        "lock_owner_dead",
        "lock_released",
        "unlock_failed",
      ].includes(e.session_event)
    ) {
      thread.operation_count++;
      thread.history = [...thread.history, e].slice(-20);
    }
  }
  return {
    graph: {
      nodes: [...nodes.values()],
      edges: [...edges.values()],
      reported_cycles: alerts,
      projection_complete: complete && sequenceComplete,
      monitoring_complete: count ? reliable : null,
    },
    threads: {
      threads: [...threads.values()].map((thread) => ({
        ...thread,
        observation_reliable: thread.observation_reliable && reliable,
      })),
      projection_complete: complete && sequenceComplete,
    },
    alerts: {
      alerts,
      total_received: alerts.length,
      truncated: !complete || truncated,
    },
  };
}
export function reportedCycleEdges(cycles: RuntimeEvent[]): Set<string> {
  return new Set(
    cycles.flatMap((e) =>
      (e.cycle || []).slice(1).map((target, i) => e.cycle![i] + ">" + target),
    ),
  );
}
