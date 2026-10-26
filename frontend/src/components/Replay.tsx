import { useEffect, useMemo, useState } from "react";
import { Play, Pause, RotateCcw, SkipBack, SkipForward } from "lucide-react";
import type { Snapshot } from "../api/types";
import { reconstruct } from "../lib/replay";
import { Alerts } from "./Alerts";
import { LockGraph } from "./LockGraph";
import { Threads } from "./Threads";
import { Timeline } from "./Timeline";
import { Badge, Details, Empty, Notice, Panel } from "./Common";
export function Replay({ snapshot }: { snapshot: Snapshot }) {
  const [position, setPosition] = useState(0),
    [playing, setPlaying] = useState(false),
    [speed, setSpeed] = useState(1);
  const items = snapshot.events.items;
  const incomplete =
    snapshot.events.gap ||
    snapshot.events.history_truncated ||
    !snapshot.session.ingestion_complete;
  const projection = useMemo(
    () =>
      reconstruct(
        items,
        position,
        snapshot.events.gap || snapshot.events.history_truncated,
      ),
    [items, position, snapshot.events.gap, snapshot.events.history_truncated],
  );
  useEffect(() => {
    if (!playing) return;
    const timer = setInterval(
      () => setPosition((p) => Math.min(p + 1, items.length)),
      800 / speed,
    );
    return () => clearInterval(timer);
  }, [playing, speed, items.length]);
  useEffect(() => {
    if (position >= items.length) setPlaying(false);
  }, [position, items.length]);
  function seek(n: number) {
    setPlaying(false);
    setPosition(n);
  }
  if (!items.length)
    return (
      <Empty title="No retained events to replay">
        This session has no recorded event history.
      </Empty>
    );
  return (
    <div className="stack">
      <Panel
        title="Recorded Session Replay"
        subtitle="Actual retained events · playback timing is illustrative, original timestamps are unchanged"
        action={<Badge value={snapshot.session.state} />}
      >
        {incomplete && (
          <Notice>
            Incomplete replay: retained events cannot establish missing
            historical state. Session outcome and playback position are
            separate.
          </Notice>
        )}
        <div className="replay-controls">
          <button aria-label="Restart replay" onClick={() => seek(0)}>
            <RotateCcw size={17} />
          </button>
          <button
            aria-label="Previous event"
            disabled={!position}
            onClick={() => seek(position - 1)}
          >
            <SkipBack size={17} />
          </button>
          <button
            className="primary"
            disabled={position === items.length}
            onClick={() => setPlaying(!playing)}
          >
            {playing ? <Pause size={16} /> : <Play size={16} />}{" "}
            {playing ? "Pause" : "Play"}
          </button>
          <button
            aria-label="Next event"
            disabled={position >= items.length}
            onClick={() => seek(position + 1)}
          >
            <SkipForward size={17} />
          </button>
          <label className="inline-label">
            Speed
            <select
              aria-label="Playback speed"
              value={speed}
              onChange={(e) => setSpeed(Number(e.target.value))}
            >
              {[0.5, 1, 2, 4].map((n) => (
                <option key={n} value={n}>
                  {n}×
                </option>
              ))}
            </select>
          </label>
          <span className="tag">
            {playing ? "Playing" : "Paused"} · {position} / {items.length}{" "}
            events
          </span>
        </div>
        <input
          className="seek"
          aria-label="Replay position"
          type="range"
          min="0"
          max={items.length}
          value={position}
          onChange={(e) => seek(Number(e.target.value))}
        />
        <div className="row-between subtle">
          <span>Before first retained event</span>
          <span>End of retained history</span>
        </div>
        {position > 0 ? (
          <details className="replay-event" open>
            <summary>
              Current recorded event: #{items[position - 1].event.sequence} ·{" "}
              {items[position - 1].event.session_event}
            </summary>
            <Details value={items[position - 1].event} />
          </details>
        ) : (
          <p className="scope-note">
            No events applied yet. Step forward to reveal recorded observations.
          </p>
        )}
      </Panel>
      <LockGraph graph={projection.graph} />
      <Alerts
        data={projection.alerts}
        sessionId={snapshot.session.session_id}
      />
      <Threads data={projection.threads} />
      <Timeline items={items} onSeek={seek} />
    </div>
  );
}
