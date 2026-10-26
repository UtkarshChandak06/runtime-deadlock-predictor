"""Project observed evidence only. All cycle decisions remain in the C analyzer."""
from collections import deque


class Projection:
    def __init__(self, limit=256):
        self.limit = limit
        self.nodes = {}
        self.edges = {}
        self.threads = {}
        self.alerts = deque(maxlen=limit)
        self.warning_count = 0
        self.complete = True

    def node(self, lock, event):
        if lock not in self.nodes:
            if len(self.nodes) == self.limit:
                self.complete = False; return
            self.nodes[lock] = {"lock_id": lock, "first_sequence": event["sequence"], "destroy_observed": False}
        if lock == event.get("lock_id"):
            for field in ("mutex_address", "lock_kind"):
                if field in event:
                    self.nodes[lock][field] = event[field]
            if event["session_event"] == "mutex_destroy":
                self.nodes[lock]["destroy_observed"] = True

    def consume(self, event, trusted):
        kind = event["session_event"]
        for lock in set(event.get("held_locks", []) + event.get("cycle", []) + ([event["lock_id"]] if "lock_id" in event else [])):
            self.node(lock, event)
        if kind == "lock_request" and trusted and "lock_id" in event:
            lock = event["lock_id"]
            held = event.get("held_locks", [])
            recursive_reentry = event.get("lock_kind") == "recursive" and lock in held
            if not recursive_reentry:
                for source in held:
                    key = (source, lock)
                    if key not in self.edges:
                        if len(self.edges) == self.limit:
                            self.complete = False; continue
                        self.edges[key] = {"from_lock": source, "to_lock": lock, "first_sequence": event["sequence"],
                                           "last_sequence": event["sequence"], "request_count": 0,
                                           "last_thread_id": event["thread_id"], "evidence": "observed_held_at_request"}
                    self.edges[key]["request_count"] += 1
                    self.edges[key]["last_sequence"] = event["sequence"]
                    self.edges[key]["last_thread_id"] = event["thread_id"]
        if kind == "potential_lock_order_cycle":
            self.alerts.append(event.copy()); self.warning_count += 1
        instance = event["thread_instance"]
        if not instance:
            return  # Main-thread metadata before a tracker exists is not a fake thread instance.
        key = (event["thread_id"], instance)
        if key not in self.threads:
            if len(self.threads) == self.limit:
                self.complete = False; return
            self.threads[key] = {"thread_id": key[0], "thread_instance": key[1], "operation_count": 0,
                                 "last_observed_held_locks": [], "last_observed_depths": [],
                                 "last_requested_lock": None, "observation_reliable": trusted,
                                 "exit_observed": False, "history": deque(maxlen=20)}
        thread = self.threads[key]
        thread["last_event"] = kind; thread["last_sequence"] = event["sequence"]
        thread["observation_reliable"] = thread["observation_reliable"] and trusted
        if "held_locks" in event:
            thread["last_observed_held_locks"] = event["held_locks"].copy()
            thread["last_observed_depths"] = event["held_depths"].copy()
        if kind == "lock_request":
            thread["last_requested_lock"] = event.get("lock_id")
        elif kind in {"lock_acquired", "lock_failed", "lock_owner_dead"}:
            thread["last_requested_lock"] = None
        elif kind == "thread_end":
            thread["exit_observed"] = True
        if kind in {"lock_request", "lock_acquired", "lock_failed", "lock_owner_dead", "lock_released", "unlock_failed"}:
            thread["operation_count"] += 1
            thread["history"].append({key: event[key] for key in ("sequence", "session_event", "lock_id", "return_code") if key in event})

    def graph(self, monitoring_complete):
        return {"nodes": list(self.nodes.values()), "edges": list(self.edges.values()),
                "reported_cycles": list(self.alerts), "projection_complete": self.complete,
                "monitoring_complete": monitoring_complete}

    def thread_view(self, reliable):
        return {"threads": [{**thread, "history": list(thread["history"]),
                              "observation_reliable": reliable and thread["observation_reliable"]}
                             for thread in self.threads.values()], "projection_complete": self.complete}
