import os
from pathlib import Path
from pydantic import BaseModel, ConfigDict, Field

ROOT = Path(__file__).resolve().parents[2]


class Settings(BaseModel):
    model_config = ConfigDict(frozen=True)
    root: Path = ROOT
    workspace_path: Path | None = None
    max_concurrent: int = Field(default=2, ge=1, le=8)
    max_sessions: int = Field(default=20, ge=1, le=100)
    max_events: int = Field(default=2000, ge=1, le=10000)
    max_output_bytes: int = Field(default=65536, ge=64, le=1048576)
    max_line_bytes: int = Field(default=4096, ge=256, le=16384)
    ws_queue: int = Field(default=128, ge=1, le=1024)
    max_ws_clients: int = Field(default=8, ge=1, le=32)
    normal_timeout: float = Field(default=5.0, ge=0.1, le=30)
    # This fixture's safety bound is intentionally not configurable via HTTP/env.
    deadlock_timeout: float = Field(default=2.5, ge=2, le=3)
    projection_limit: int = Field(default=256, ge=2, le=1024)
    origins: tuple[str, ...] = ("http://127.0.0.1:5173", "http://localhost:5173",
                               "http://127.0.0.1:8000", "http://localhost:8000")

    @classmethod
    def from_env(cls):
        values = {'workspace_path': Path(os.environ.get('DS_WORKSPACE_DIR',str(ROOT / '.workspace')))}
        for field in ("max_concurrent", "max_sessions", "max_events", "max_output_bytes",
                      "max_line_bytes", "ws_queue", "max_ws_clients", "projection_limit"):
            if raw := os.getenv("DS_" + field.upper()):
                values[field] = int(raw)
        if raw := os.getenv("DS_NORMAL_TIMEOUT"):
            values["normal_timeout"] = float(raw)
        return cls(**values)
