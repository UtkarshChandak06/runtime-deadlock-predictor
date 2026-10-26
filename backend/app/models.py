from datetime import datetime
from typing import Any, Literal
from pydantic import BaseModel, ConfigDict, Field

State = Literal["created", "starting", "running", "completed", "failed", "timed_out", "stopped"]
TERMINAL = {"completed", "failed", "timed_out", "stopped"}


class SessionCreate(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True)
    application_id: str = Field(min_length=1, max_length=64, pattern=r"^[a-z_]+$")
    auto_start: bool = True


class ApplicationView(BaseModel):
    application_id: str
    display_name: str
    description: str
    scenario: str
    safe_to_run_normally: bool
    bounded_supervision: bool
    executable: str  # Repository-relative public reference, not an arbitrary path input.
    available: bool
    unavailable_reason: str | None = None


class SessionView(BaseModel):
    session_id: str
    application_id: str
    state: State
    created_at: datetime
    started_at: datetime | None
    ended_at: datetime | None
    pid: int | None
    exit_code: int | None
    event_count: int
    warning_count: int
    invalid_records: int
    c_monitor_complete: bool | None
    ingestion_complete: bool
    monitoring_complete: bool | None
    history_truncated: bool
    dropped_events: int
    stdout: str
    stderr: str
    stdout_truncated: bool
    stderr_truncated: bool
    error: str | None
    cursor: int
    cleanup_verified: bool
    controlled_deadlock_evidence: dict[str, Any] | None


class EventItem(BaseModel):
    index: int
    event: dict[str, Any]


class EventPage(BaseModel):
    items: list[EventItem]
    next_after: int
    oldest_available: int
    total_received: int
    gap: bool
    history_truncated: bool


class ProjectionView(BaseModel):
    nodes: list[dict[str, Any]]
    edges: list[dict[str, Any]]
    reported_cycles: list[dict[str, Any]]
    projection_complete: bool
    monitoring_complete: bool | None
    semantics: str = "historical request dependencies; C-reported cycles only"


class ThreadsView(BaseModel):
    threads: list[dict[str, Any]]
    projection_complete: bool
    semantics: str = "last observed per-thread state, not live scheduler state"


class AlertsView(BaseModel):
    alerts: list[dict[str, Any]]
    total_received: int
    truncated: bool


class HealthView(BaseModel):
    status: str = "ok"
    active_sessions: int
    retained_sessions: int
    phase: int = 5


class StreamMessage(BaseModel):
    type: Literal["session_snapshot", "runtime_event", "session_status", "monitoring_error"]
    session_id: str
    cursor: int
    data: dict[str, Any]
