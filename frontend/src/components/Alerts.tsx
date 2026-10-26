import { TriangleAlert, ShieldCheck, ArrowUpRight } from "lucide-react";
import type { Alerts as AlertView } from "../api/types";
import { Completeness, Details, Notice, Panel } from "./Common";
export function Alerts({
  data,
  sessionId,
  onGraph,
}: {
  data: AlertView;
  sessionId: string;
  onGraph?: () => void;
}) {
  return (
    <Panel
      title="Alert investigation"
      subtitle="Potential lock-order cycles reported by the C analyzer"
      action={<span className="tag">{data.total_received} warnings</span>}
    >
      <p className="scope-note">
        A potential cycle does not prove the application is currently
        deadlocked.
      </p>
      {data.truncated && (
        <Notice>
          Only retained warnings are available. Earlier evidence may be missing.
        </Notice>
      )}
      {!data.alerts.length ? (
        <div className="empty">
          <ShieldCheck size={32} />
          <h3>No potential-cycle warnings in this view</h3>
          <p>
            This is the observed warning count, not a guarantee that every
            possible execution is safe.
          </p>
        </div>
      ) : (
        data.alerts.map((e) => (
          <article className="alert-card" key={String(e.sequence)}>
            <div className="row-between">
              <h3>
                <TriangleAlert size={18} /> Potential Lock-Order Cycle
              </h3>
              <span className="mono subtle">Event #{e.sequence}</span>
            </div>
            <div className="cycle-path">{e.cycle?.join(" → ")}</div>
            <dl className="evidence-grid">
              <dt>Session</dt>
              <dd className="mono">{sessionId}</dd>
              <dt>Thread / instance</dt>
              <dd>
                {e.thread_id} / {e.thread_instance}
              </dd>
              <dt>Held → requested</dt>
              <dd>
                {e.held_lock} → {e.requested_lock}
              </dd>
              <dt>Closing dependency</dt>
              <dd>{e.closing_edge?.join(" → ")}</dd>
            </dl>
            <Completeness value={e.monitoring_complete} />
            <p>
              The C analyzer reported this closed path among historical request
              dependencies. Review whether the participating locks can be
              requested in one consistent order; this is general investigation
              guidance, not a verified source-level fix.
            </p>
            <details>
              <summary>Original C warning evidence</summary>
              <Details value={e} />
            </details>
            {onGraph && (
              <button className="text-button" onClick={onGraph}>
                Inspect reported cycle in graph <ArrowUpRight size={15} />
              </button>
            )}
          </article>
        ))
      )}
    </Panel>
  );
}
