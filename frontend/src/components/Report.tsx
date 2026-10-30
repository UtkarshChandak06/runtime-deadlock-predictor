import { Download, Printer } from "lucide-react";
import type { Snapshot, Application } from "../api/types";
import { json } from "../api/client";
import { Badge, Completeness, Details, Panel } from "./Common";
export function reportData(snapshot: Snapshot, application?: Application) {
  return {
    format: "deadlock-sentinel-session-report-v1",
    integer_encoding:
      "Unsafe JSON integers are represented as exact decimal strings.",
    limitations: [
      "Historical cycles are potential risks, not proof of current deadlock.",
      "Thread views are last observed.",
      "Saved benchmark measurements are separate from this session report.",
      "Zero warnings do not prove safety. Unsupported operations can be invisible.",
      "User-program same-process instrumentation is not tamper-proof.",
      "Replay presents retained observations and does not execute the program.",
    ],
    ...snapshot,
    application: application ?? null,
  };
}
export function Report({
  snapshot,
  application,
}: {
  snapshot: Snapshot;
  application?: Application;
}) {
  const { session: s, graph, threads, alerts } = snapshot;
  function download() {
    const url = URL.createObjectURL(
      new Blob([json(reportData(snapshot, application))], {
        type: "application/json",
      }),
    );
    const anchor = document.createElement("a");
    anchor.href = url;
    anchor.download = `deadlock-sentinel-${s.session_id}.json`;
    anchor.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  }
  return (
    <div className="stack">
      <Panel
        title="Session report"
        subtitle="Captured runtime evidence · no benchmark estimates"
        action={
          <div className="actions">
            <button onClick={() => window.print()}>
              <Printer size={16} /> Print
            </button>
            <button className="primary" onClick={download}>
              <Download size={16} /> Export JSON
            </button>
          </div>
        }
      >
        <dl className="evidence-grid">
          <dt>Application</dt>
          <dd>{s.application_id}</dd>
          <dt>Scenario</dt>
          <dd>{s.target_kind === "user_build" ? "User-supplied build; no expected result assigned" : application?.scenario || "Catalog metadata unavailable"}</dd>
          <dt>Observed activity</dt><dd>{s.observation?.replaceAll("_", " ") || "See event history"}. Zero warnings do not prove safety.</dd>
          <dt>Exit code</dt><dd>{s.exit_code ?? "Not exited"}</dd>
          <dt>Client continuity / history gap</dt>
          <dd>
            {snapshot.client_continuity_lost
              ? "Interrupted"
              : "No interruption recorded"}{" "}
            /{" "}
            {snapshot.events.gap
              ? "Missing retained evidence"
              : "No gap reported"}
          </dd>
          <dt>Session</dt>
          <dd className="mono">{s.session_id}</dd>
          <dt>Execution outcome</dt>
          <dd>
            <Badge value={s.state} />
          </dd>
          <dt>Started / ended</dt>
          <dd>
            {s.started_at || "Not started"} / {s.ended_at || "Not ended"}
          </dd>
          <dt>Observed data</dt>
          <dd>
            {s.event_count} events · {threads.threads.length} threads ·{" "}
            {graph.nodes.length} locks · {graph.edges.length} dependencies
          </dd>
          <dt>C warnings</dt>
          <dd>{alerts.total_received}</dd>
          <dt>Monitoring</dt>
          <dd>
            <Completeness value={s.monitoring_complete} />
          </dd>
          <dt>C / ingestion completeness</dt>
          <dd>
            {String(s.c_monitor_complete)} / {String(s.ingestion_complete)}
          </dd>
          <dt>Retention / invalid records</dt>
          <dd>
            {s.dropped_events} dropped · {s.invalid_records} invalid
          </dd>
          <dt>Projection completeness</dt>
          <dd>
            Graph {String(graph.projection_complete)} · Threads{" "}
            {String(threads.projection_complete)}
          </dd>
          <dt>Cleanup verified</dt>
          <dd>{s.cleanup_verified ? "Yes" : "Not yet verified"}</dd>
          <dt>Failure information</dt>
          <dd>{s.error || "None reported"}</dd>
        </dl>
        {s.controlled_deadlock_evidence && (
          <>
            <h3>Controlled-fixture evidence</h3>
            <Details value={s.controlled_deadlock_evidence} />
          </>
        )}
        {s.build_metadata && <><h3>Exact source and build</h3><Details value={s.build_metadata} /></>}
        <p className="scope-note">
          Saved benchmark measurements are available in Performance. Historical
          cycles do not prove a current deadlock, and thread snapshots are not
          scheduler state.
        </p>
      </Panel>
      <div className="two-column">
        <Panel
          title="Application stdout"
          subtitle={
            s.stdout_truncated
              ? "Retained prefix only — output truncated"
              : "Separate from the structured monitor stream"
          }
        >
          <pre className="console">{s.stdout || "No stdout captured."}</pre>
        </Panel>
        <Panel
          title="Application stderr"
          subtitle={
            s.stderr_truncated
              ? "Retained prefix only — output truncated"
              : "Captured application / loader diagnostics"
          }
        >
          <pre className="console">{s.stderr || "No stderr captured."}</pre>
        </Panel>
      </div>
    </div>
  );
}
