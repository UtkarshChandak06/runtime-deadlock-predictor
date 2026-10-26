import { useMemo, useState } from "react";
import type { EventItem } from "../api/types";
import { Details, Empty, Panel, words } from "./Common";
export function Timeline({
  items,
  onSeek,
}: {
  items: EventItem[];
  onSeek?: (index: number) => void;
}) {
  const [kind, setKind] = useState(""),
    [thread, setThread] = useState(""),
    [lock, setLock] = useState(""),
    [page, setPage] = useState(0),
    [selected, setSelected] = useState<EventItem | null>(null);
  const filtered = useMemo(
    () =>
      items.filter(
        ({ event: e }) =>
          (!kind || e.session_event === kind) &&
          (!thread || String(e.thread_id) === thread) &&
          (!lock ||
            [e.lock_id, ...(e.held_locks || []), ...(e.cycle || [])].some(
              (id) => id?.includes(lock),
            )),
      ),
    [items, kind, thread, lock],
  );
  const pages = Math.max(1, Math.ceil(filtered.length / 50)),
    current = Math.min(page, pages - 1);
  return (
    <Panel
      title="Event timeline"
      subtitle="Original monitor sequence · CLOCK_MONOTONIC nanoseconds"
    >
      <div className="filters">
        <label>
          Event type
          <select
            aria-label="Event type"
            value={kind}
            onChange={(e) => {
              setKind(e.target.value);
              setPage(0);
            }}
          >
            <option value="">All event types</option>
            {[...new Set(items.map((i) => i.event.session_event))].map(
              (type) => (
                <option key={type} value={type}>
                  {words(type)}
                </option>
              ),
            )}
          </select>
        </label>
        <label>
          Thread
          <select
            value={thread}
            onChange={(e) => {
              setThread(e.target.value);
              setPage(0);
            }}
          >
            <option value="">All threads</option>
            {[...new Set(items.map((i) => String(i.event.thread_id)))].map(
              (id) => (
                <option key={id}>{id}</option>
              ),
            )}
          </select>
        </label>
        <label>
          Lock identity
          <input
            aria-label="Search lock ID"
            placeholder="Search lock ID…"
            value={lock}
            onChange={(e) => {
              setLock(e.target.value);
              setPage(0);
            }}
          />
        </label>
      </div>
      {filtered.length ? (
        <>
          <div className="table-scroll">
            <table>
              <thead>
                <tr>
                  <th>Sequence</th>
                  <th>Runtime event</th>
                  <th>Thread / instance</th>
                  <th>Lock</th>
                  <th>Monotonic ns</th>
                  <th>Evidence</th>
                </tr>
              </thead>
              <tbody>
                {filtered
                  .slice(current * 50, (current + 1) * 50)
                  .map((item) => (
                    <tr
                      key={item.index}
                      className={item.event.cycle ? "warning-row" : ""}
                    >
                      <td className="mono">#{item.event.sequence}</td>
                      <td>
                        <span
                          className={`event-dot ${item.event.cycle ? "red" : ""}`}
                        />
                        {words(item.event.session_event)}
                      </td>
                      <td className="mono">
                        {item.event.thread_id} / {item.event.thread_instance}
                      </td>
                      <td className="mono">
                        {item.event.lock_id || item.event.requested_lock || "—"}
                      </td>
                      <td className="mono subtle">{item.event.timestamp_ns}</td>
                      <td>
                        <button
                          onClick={() => setSelected(item)}
                          aria-label={`Inspect event ${item.event.sequence}`}
                        >
                          Inspect
                        </button>
                        {onSeek && (
                          <button
                            onClick={() =>
                              onSeek(
                                items.findIndex((i) => i.index === item.index) +
                                  1,
                              )
                            }
                          >
                            Seek
                          </button>
                        )}
                      </td>
                    </tr>
                  ))}
              </tbody>
            </table>
          </div>
          <div className="pagination">
            <span>
              {filtered.length} matching events · page {current + 1} / {pages}
            </span>
            <button disabled={!current} onClick={() => setPage(current - 1)}>
              Previous page
            </button>
            <button
              disabled={current + 1 >= pages}
              onClick={() => setPage(current + 1)}
            >
              Next page
            </button>
          </div>
        </>
      ) : (
        <Empty
          title={items.length ? "No matching events" : "No events observed"}
        >
          Events appear only after actual monitor records arrive.
        </Empty>
      )}
      {selected && (
        <div className="selection">
          <div className="row-between">
            <h3>Original event #{selected.event.sequence}</h3>
            <button onClick={() => setSelected(null)}>Close event</button>
          </div>
          <Details value={selected.event} />
        </div>
      )}
    </Panel>
  );
}
