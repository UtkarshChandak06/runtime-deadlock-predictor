import { ArrowUpRight, Play, ShieldCheck, Shuffle, Timer } from "lucide-react";
import type { Application } from "../api/types";
import { Notice, Panel } from "../components/Common";
export function Launcher({
  applications,
  busy,
  onStart,
}: {
  applications: Application[];
  busy: boolean;
  onStart: (app: Application) => void;
}) {
  return (
    <Panel
      title="Run a monitored application"
      subtitle="Choose a real pthread workload. Every observation comes from the C monitor."
      action={
        <span className="tag">
          Approved catalog <ArrowUpRight size={13} />
        </span>
      }
    >
      <div className="application-grid">
        {applications.map((app) => {
          const bounded = app.bounded_supervision,
            disabled =
              !app.available || (!app.safe_to_run_normally && !bounded);
          const Icon = bounded
            ? Timer
            : app.scenario === "safe"
              ? ShieldCheck
              : Shuffle;
          return (
            <article
              className={`application-card ${bounded ? "bounded" : ""}`}
              key={app.application_id}
            >
              <span className="app-icon">
                <Icon size={21} />
              </span>
              <span className="eyebrow">
                {app.scenario.replaceAll("_", " ")}
              </span>
              <h3>{app.display_name}</h3>
              <p>{app.description}</p>
              <p className="education">
                {bounded
                  ? "Study conflicting requests under a bounded supervisor."
                  : app.scenario === "safe"
                    ? "Observe consistent account lock ordering."
                    : "Inspect why opposite historical orders indicate risk."}
              </p>
              {bounded && (
                <Notice>
                  Intentionally blocks threads. The backend terminates the
                  fixture after its bounded timeout.
                </Notice>
              )}
              {disabled && (
                <p className="warn">
                  {app.unavailable_reason ||
                    "Bounded execution support unavailable."}
                </p>
              )}
              <button
                className={bounded ? "" : "primary"}
                disabled={disabled || busy}
                onClick={() => onStart(app)}
                aria-label={`Start monitoring ${app.display_name}`}
              >
                <Play size={14} />
                {busy ? "Starting…" : "Start Monitoring"}
              </button>
            </article>
          );
        })}
      </div>
    </Panel>
  );
}
