import { useState } from "react";
import type { Threads as ThreadView, Thread } from "../api/types";
import { Details, Empty, Notice, Panel, words } from "./Common";
export function Threads({ data }: { data: ThreadView }) {
  const [selected, setSelected] = useState<string | null>(null);
  const detail = data.threads.find(
    (t) => t.thread_id + ":" + t.thread_instance === selected,
  );
  const key = (t: Thread) => t.thread_id + ":" + t.thread_instance;
  return (
    <Panel
      title="Thread explorer"
      subtitle="Last-observed per-thread state, not a kernel scheduling snapshot"
    >
      {!data.projection_complete && (
        <Notice>Thread projection is incomplete.</Notice>
      )}
      {data.threads.length ? (
        <div className="thread-grid">
          {data.threads.map((t) => (
            <button
              className="thread-card"
              key={key(t)}
              onClick={() => setSelected(key(t))}
            >
              <div className="row-between">
                <strong>Thread {t.thread_id}</strong>
                <span className="tag">Instance {t.thread_instance}</span>
              </div>
              <p>{t.operation_count} observed operations</p>
              <dl>
                <dt>Last event</dt>
                <dd>{words(t.last_event)}</dd>
                <dt>Last observed held</dt>
                <dd>
                  {t.last_observed_held_locks.join(", ") || "None recorded"}
                </dd>
                <dt>Last observed request</dt>
                <dd>{t.last_requested_lock || "None recorded"}</dd>
                <dt>Evidence</dt>
                <dd className={t.observation_reliable ? "good" : "warn"}>
                  {t.observation_reliable
                    ? "Within observed scope"
                    : "Limited / unknown state"}
                </dd>
              </dl>
            </button>
          ))}
        </div>
      ) : (
        <Empty title="No tracked thread instances" />
      )}
      {detail && (
        <div className="selection">
          <div className="row-between">
            <h3>
              Thread {detail.thread_id} · instance {detail.thread_instance}
            </h3>
            <button onClick={() => setSelected(null)}>Close thread</button>
          </div>
          <Details value={detail} />
        </div>
      )}
    </Panel>
  );
}
