"""Validate the existing C JSONL v1 schema; never manufacture event fields."""
from dataclasses import dataclass
import errno
import json
from typing import Annotated, Literal
from pydantic import BaseModel, ConfigDict, Field, ValidationError, model_validator

UInt = Annotated[int, Field(strict=True, ge=0, le=2**64 - 1)]
Positive = Annotated[int, Field(strict=True, ge=1, le=2**64 - 1)]
Lock = Annotated[str, Field(strict=True, pattern=r"^lock_[1-9a-f][0-9a-f]*$", max_length=21)]
EventType = Literal["session_start", "session_end", "thread_start", "thread_end", "mutex_init",
                    "mutex_init_failed", "mutex_destroy", "mutex_destroy_failed", "mutex_kind_unknown",
                    "lock_request", "lock_acquired", "lock_failed", "lock_owner_dead", "lock_released",
                    "unlock_failed", "potential_lock_order_cycle", "monitoring_incomplete", "sink_error"]
RESULTS = {"mutex_init", "mutex_init_failed", "mutex_destroy", "mutex_destroy_failed",
           "lock_acquired", "lock_failed", "lock_owner_dead", "lock_released", "unlock_failed"}
SUCCESS = {"mutex_init", "mutex_destroy", "lock_acquired", "lock_released"}


class RuntimeEvent(BaseModel):
    model_config = ConfigDict(strict=True, extra="allow")
    schema_version: Literal[1]
    session_event: EventType
    timestamp_ns: UInt
    process_id: Positive
    thread_id: Positive
    thread_instance: UInt
    sequence: Positive
    monitoring_complete: bool
    lock_id: Lock | None = None
    mutex_address: Annotated[str, Field(pattern=r"^0x[0-9a-f]+$", max_length=18)] | None = None
    lock_kind: Literal["normal", "recursive", "errorcheck", "unknown"] | None = None
    return_code: Annotated[int, Field(ge=0, le=2**31 - 1)] | None = None
    monitor_status: Annotated[int, Field(ge=1, le=2**31 - 1)] | None = None
    reason: Annotated[str, Field(min_length=1, max_length=256)] | None = None
    held_locks: Annotated[list[Lock], Field(max_length=16)] | None = None
    held_depths: Annotated[list[Positive], Field(max_length=16)] | None = None
    held_lock: Lock | None = None
    requested_lock: Lock | None = None
    closing_edge: Annotated[list[Lock], Field(min_length=2, max_length=2)] | None = None
    cycle: Annotated[list[Lock], Field(min_length=2, max_length=33)] | None = None
    graph_sequence: Positive | None = None

    @model_validator(mode="after")
    def validate_shape(self):
        event = self.session_event
        if any(name in type(self).model_fields and getattr(self, name) is None for name in self.model_fields_set):
            raise ValueError("optional fields must be omitted rather than null")
        if type(self.schema_version) is not int:
            raise ValueError("schema_version must be integer 1")
        if (self.held_locks is None) != (self.held_depths is None):
            raise ValueError("held snapshot needs parallel depth array")
        if self.held_locks is not None:
            if len(self.held_locks) != len(set(self.held_locks)) or len(self.held_locks) != len(self.held_depths):
                raise ValueError("invalid held snapshot")
        if event in RESULTS:
            if self.return_code is None:
                raise ValueError("operation result is required")
            if event in SUCCESS and self.return_code != 0:
                raise ValueError("successful operation must return zero")
            if event not in SUCCESS and self.return_code == 0:
                raise ValueError("failed/special operation must return nonzero")
            if event == "lock_owner_dead" and self.return_code != errno.EOWNERDEAD:
                raise ValueError("owner-dead result mismatch")
        elif self.return_code is not None:
            raise ValueError("non-result event cannot claim a pthread return value")
        if event in {"monitoring_incomplete", "sink_error"}:
            if self.monitoring_complete or not self.reason:
                raise ValueError("incomplete notification requires false flag and reason")
        if event == "potential_lock_order_cycle":
            if not self.cycle or not self.graph_sequence or not self.held_lock or not self.requested_lock:
                raise ValueError("warning fields missing")
            if self.cycle[0] != self.cycle[-1] or self.cycle[0] != self.requested_lock:
                raise ValueError("cycle endpoints mismatch")
            if self.cycle[-2] != self.held_lock or self.closing_edge != [self.held_lock, self.requested_lock]:
                raise ValueError("closing edge mismatch")
        if self.monitoring_complete and event in {"lock_request", "lock_acquired", "lock_failed", "lock_released", "unlock_failed"}:
            if not self.lock_id or not self.thread_instance or self.held_locks is None or self.lock_kind is None:
                raise ValueError("complete lock event requires identity and snapshot")
        if event == "mutex_init" and self.monitoring_complete and (not self.lock_id or not self.lock_kind):
            raise ValueError("initialized mutex metadata missing")
        if event == "mutex_kind_unknown" and (not self.lock_id or self.lock_kind != "unknown" or not self.reason):
            raise ValueError("unknown-kind metadata missing")
        return self


@dataclass(frozen=True)
class ParseIssue:
    reason: str
    preview: str = ""


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON key")
        result[key] = value
    return result


def reject_constant(value):
    raise ValueError("nonfinite JSON number")


def parse_record(line: bytes):
    try:
        raw = json.loads(line.decode("utf-8"), object_pairs_hook=unique_object, parse_constant=reject_constant)
        if not isinstance(raw, dict) or type(raw.get("schema_version")) is not int:
            raise ValueError("expected a versioned JSON object")
        RuntimeEvent.model_validate(raw)
        return raw  # Preserve original fields and integer values, without guessed defaults.
    except (ValueError, UnicodeError, ValidationError, RecursionError) as error:
        return ParseIssue(type(error).__name__ + ": invalid JSONL v1 event", line[:96].decode("utf-8", "replace"))


class JSONLParser:
    def __init__(self, limit=4096):
        self.limit = limit
        self.buffer = bytearray()
        self.discarding = False

    def feed(self, data: bytes):
        results = []
        for piece in data.splitlines(keepends=True):
            # bytes.splitlines also splits CR: only LF terminates our protocol.
            ended = piece.endswith(b"\n")
            if not self.discarding:
                if len(self.buffer) + len(piece) > self.limit:
                    preview = (bytes(self.buffer[:96]) + piece[:96])[:96]
                    results.append(ParseIssue("line_size_limit", preview.decode("utf-8", "replace")))
                    self.buffer.clear(); self.discarding = True
                else:
                    self.buffer.extend(piece)
            if ended:
                if not self.discarding:
                    results.append(parse_record(bytes(self.buffer[:-1])))
                self.buffer.clear(); self.discarding = False
        return results

    def finish(self):
        result = [ParseIssue("partial_record_at_eof")] if self.buffer and not self.discarding else []
        self.buffer.clear(); self.discarding = False
        return result
