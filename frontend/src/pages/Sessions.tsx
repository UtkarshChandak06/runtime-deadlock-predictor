import type { Session } from "../api/types";
import { terminal } from "../api/types";
import { Badge, Completeness, Empty, Panel } from "../components/Common";
import { request } from "../api/client";
import { useState } from "react";
export function Sessions({
  sessions,
  onSelect,
  onReplay,
  onRefresh,
}: {
  sessions: Session[];
  onSelect: (id: string) => void;
  onReplay: (id: string) => void;
  onRefresh: () => void;
}) {
  const [error,setError]=useState("");
  return (
    <Panel
      title="Retained sessions"
      subtitle="Bounded retained history. The configured local workspace persists runs across restarts. Replay never launches a program."
      action={<button onClick={onRefresh}>Refresh sessions</button>}
    >
      {error&&<p role="alert">{error}</p>}
      {sessions.length ? (
        <div className="table-scroll">
          <table>
            <thead>
              <tr>
                <th>Application / session</th>
                <th>Execution</th>
                <th>Started / ended</th>
                <th>Events / warnings</th>
                <th>Completeness</th>
                <th>Open</th>
              </tr>
            </thead>
            <tbody>
              {[...sessions]
                .sort((a, b) => b.created_at.localeCompare(a.created_at))
                .map((s) => (
                  <tr key={s.session_id}>
                    <td>
                      <strong>{s.application_id}</strong>
                      <small className="mono">{s.session_id}</small>
                      {s.error && <small className="warn">{s.error}</small>}
                    </td>
                    <td>
                      <Badge value={s.state} />
                    </td>
                    <td>
                      <small>
                        {s.started_at
                          ? new Date(s.started_at).toLocaleString()
                          : "Not started"}
                      </small>
                      <small>
                        {s.ended_at
                          ? new Date(s.ended_at).toLocaleString()
                          : "Not ended"}
                      </small>
                    </td>
                    <td>
                      {s.event_count} / {s.warning_count}
                    </td>
                    <td>
                      <Completeness value={s.monitoring_complete} />
                    </td>
                    <td>
                      <div className="actions">
                        <button onClick={() => onSelect(s.session_id)}>
                          Inspect
                        </button>
                        <button
                          disabled={!terminal(s.state) || !s.event_count}
                          onClick={() => onReplay(s.session_id)}
                        >
                          Replay
                        </button>
                        <button disabled={!terminal(s.state)} onClick={()=>{void request(`/sessions/${s.session_id}`,{method:"DELETE"}).then(onRefresh).catch(e=>setError(e.message));}}>Delete</button>
                      </div>
                    </td>
                  </tr>
                ))}
            </tbody>
          </table>
        </div>
      ) : (
        <Empty title="No retained sessions">
          Start a monitored application to create real session history.
        </Empty>
      )}
    </Panel>
  );
}
