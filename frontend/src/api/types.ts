export type Integer = number | string;
export type Outcome =
  | "created"
  | "starting"
  | "running"
  | "completed"
  | "failed"
  | "timed_out"
  | "stopped";
export const terminal = (state?: Outcome) =>
  !!state && ["completed", "failed", "timed_out", "stopped"].includes(state);
export interface Application {
  application_id: string;
  display_name: string;
  description: string;
  scenario: string;
  safe_to_run_normally: boolean;
  bounded_supervision: boolean;
  executable: string;
  available: boolean;
  unavailable_reason: string | null;
}
export interface Session {
  target_kind?: "example" | "user_build";
  build_metadata?: Record<string, unknown> | null;
  mutex_activity?: number;
  observation?: string;
  session_id: string;
  application_id: string;
  state: Outcome;
  created_at: string;
  started_at: string | null;
  ended_at: string | null;
  pid: number | null;
  exit_code: number | null;
  event_count: number;
  warning_count: number;
  invalid_records: number;
  c_monitor_complete: boolean | null;
  ingestion_complete: boolean;
  monitoring_complete: boolean | null;
  history_truncated: boolean;
  dropped_events: number;
  stdout: string;
  stderr: string;
  stdout_truncated: boolean;
  stderr_truncated: boolean;
  error: string | null;
  cursor: number;
  cleanup_verified: boolean;
  controlled_deadlock_evidence: Record<string, unknown> | null;
}
export interface RuntimeEvent {
  schema_version: 1;
  session_event: string;
  timestamp_ns: Integer;
  process_id: number;
  thread_id: Integer;
  thread_instance: Integer;
  sequence: Integer;
  monitoring_complete: boolean;
  lock_id?: string;
  mutex_address?: string;
  lock_kind?: string;
  held_locks?: string[];
  held_depths?: number[];
  held_lock?: string;
  requested_lock?: string;
  closing_edge?: string[];
  cycle?: string[];
  graph_sequence?: Integer;
  return_code?: number;
  reason?: string;
  [key: string]: unknown;
}
export interface EventItem {
  index: number;
  event: RuntimeEvent;
}
export interface EventPage {
  items: EventItem[];
  next_after: number;
  oldest_available: number;
  total_received: number;
  gap: boolean;
  history_truncated: boolean;
}
export interface LockNode {
  lock_id: string;
  first_sequence: Integer;
  destroy_observed: boolean;
  mutex_address?: string;
  lock_kind?: string;
}
export interface Dependency {
  from_lock: string;
  to_lock: string;
  first_sequence: Integer;
  last_sequence: Integer;
  request_count: number;
  last_thread_id: Integer;
  evidence: string;
}
export interface Graph {
  nodes: LockNode[];
  edges: Dependency[];
  reported_cycles: RuntimeEvent[];
  projection_complete: boolean;
  monitoring_complete: boolean | null;
  semantics?: string;
}
export interface Thread {
  thread_id: Integer;
  thread_instance: Integer;
  operation_count: number;
  last_observed_held_locks: string[];
  last_observed_depths: number[];
  last_requested_lock: string | null;
  observation_reliable: boolean;
  exit_observed: boolean;
  history: Partial<RuntimeEvent>[];
  last_event: string;
  last_sequence: Integer;
}
export interface Threads {
  threads: Thread[];
  projection_complete: boolean;
  semantics?: string;
}
export interface Alerts {
  alerts: RuntimeEvent[];
  total_received: number;
  truncated: boolean;
}
export interface Snapshot {
  client_continuity_lost?: boolean;
  session: Session;
  events: EventPage;
  graph: Graph;
  threads: Threads;
  alerts: Alerts;
  errors: { code: string }[];
}
export type Envelope = { session_id: string; cursor: number } & (
  | { type: "session_snapshot"; data: Snapshot }
  | { type: "runtime_event"; data: EventItem }
  | { type: "session_status"; data: Session }
  | { type: "monitoring_error"; data: { code: string; recovery?: string } }
);
