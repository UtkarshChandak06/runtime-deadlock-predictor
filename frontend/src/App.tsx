import { useEffect, useRef, useState } from "react";
import {
  Activity,
  ArrowRight,
  Bell,
  Blocks,
  ChevronRight,
  CircleHelp,
  FileText,
  GitBranch,
  History,
  LayoutDashboard,
  List,
  Menu,
  RefreshCw,
  Shield,
  Square,
  Users,
  Wifi,
  X,
  Gauge,
} from "lucide-react";
import { api } from "./api/client";
import { terminal, type Application, type Session } from "./api/types";
import { useSession } from "./hooks/useSession";
import { Badge, Completeness, Empty, Notice, Panel } from "./components/Common";
import { LockGraph } from "./components/LockGraph";
import { Threads } from "./components/Threads";
import { Timeline } from "./components/Timeline";
import { Alerts } from "./components/Alerts";
import { Replay } from "./components/Replay";
import { Report } from "./components/Report";
import { Launcher } from "./pages/Launcher";
import { Sessions } from "./pages/Sessions";
import { Performance } from "./pages/Performance";
import { DemoHelp } from "./components/DemoHelp";
const pages = [
  ["overview", "Overview", LayoutDashboard],
  ["live", "Live Monitor", Activity],
  ["graph", "Lock Graph", GitBranch],
  ["threads", "Thread Explorer", Users],
  ["alerts", "Alerts", Bell],
  ["timeline", "Event Timeline", List],
  ["sessions", "Sessions & Replay", History],
  ["reports", "Reports", FileText],
  ["performance", "Performance", Gauge],
] as const;
type Page = (typeof pages)[number][0];
function route() {
  const params = new URLSearchParams(location.hash.slice(1));
  return {
    page: (pages.some((p) => p[0] === params.get("view"))
      ? params.get("view")
      : "overview") as Page,
    id: params.get("session"),
    replay: params.get("replay") === "1",
  };
}
export default function App() {
  const [locationState, setLocationState] = useState(route),
    [mobile, setMobile] = useState(false);
  const [narrow, setNarrow] = useState(
    () => window.matchMedia("(max-width: 640px)").matches,
  );
  useEffect(() => {
    const query = window.matchMedia("(max-width: 640px)");
    const resize = () => setNarrow(query.matches);
    const escape = (event: KeyboardEvent) => {
      if (event.key === "Escape") setMobile(false);
    };
    query.addEventListener("change", resize);
    window.addEventListener("keydown", escape);
    return () => {
      query.removeEventListener("change", resize);
      window.removeEventListener("keydown", escape);
    };
  }, []);
  const [catalog, setCatalog] = useState<Application[]>([]),
    [sessions, setSessions] = useState<Session[]>([]),
    [backend, setBackend] = useState("Connecting"),
    [error, setError] = useState(""),
    [loading, setLoading] = useState(true),
    [busy, setBusy] = useState(false),
    [revision, setRevision] = useState(0),
    [pendingStart, setPendingStart] = useState<string | null>(null);
  const launchGuard = useRef(false),
    mounted = useRef(true);
  const { page, id, replay } = locationState;
  const live = useSession(id, pendingStart === id, revision),
    snapshot = live.snapshot?.session.session_id === id ? live.snapshot : null,
    s = snapshot?.session;
  useEffect(() => {
    mounted.current = true;
    const listener = () => {
      setLocationState(route());
      setMobile(false);
      window.scrollTo(0, 0);
    };
    window.addEventListener("hashchange", listener);
    return () => {
      mounted.current = false;
      window.removeEventListener("hashchange", listener);
    };
  }, []);
  function navigate(next: Page, session = id, replayMode = false) {
    location.hash = new URLSearchParams({
      view: next,
      ...(session ? { session } : {}),
      ...(replayMode ? { replay: "1" } : {}),
    }).toString();
    setMobile(false);
  }
  async function refresh(signal?: AbortSignal) {
    setLoading(true);
    try {
      const [apps, list] = await Promise.all([
        api.applications(signal),
        api.sessions(signal),
      ]);
      if (mounted.current) {
        setCatalog(apps);
        setSessions(list);
        setBackend("Connected");
        setError("");
      }
    } catch (e) {
      if (mounted.current && (e as Error).name !== "AbortError") {
        setBackend("Unavailable");
        setError((e as Error).message);
      }
    } finally {
      if (mounted.current) setLoading(false);
    }
  }
  useEffect(() => {
    const controller = new AbortController();
    void refresh(controller.signal);
    return () => controller.abort();
  }, []);
  useEffect(() => {
    if (s)
      setSessions((old) => [
        ...old.filter((item) => item.session_id !== s.session_id),
        s,
      ]);
  }, [s]);
  async function start(app: Application) {
    if (launchGuard.current) return;
    launchGuard.current = true;
    setBusy(true);
    setError("");
    try {
      const session = await api.create(app.application_id);
      if (mounted.current) {
        setPendingStart(session.session_id);
        navigate("live", session.session_id);
      }
    } catch (e) {
      if (mounted.current) setError((e as Error).message);
    } finally {
      launchGuard.current = false;
      if (mounted.current) setBusy(false);
    }
  }
  async function control(action: "start" | "stop") {
    if (!id || busy) return;
    setBusy(true);
    try {
      await api[action](id);
      setRevision((n) => n + 1);
      await refresh();
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setBusy(false);
    }
  }
  const title = pages.find((p) => p[0] === page)![1];
  const appName =
    catalog.find((app) => app.application_id === s?.application_id)
      ?.display_name || s?.application_id;
  const duration =
    s?.started_at && s.ended_at
      ? ((Date.parse(s.ended_at) - Date.parse(s.started_at)) / 1000).toFixed(
          3,
        ) + " s"
      : s?.started_at
        ? "In progress"
        : "—";
  const incomplete =
    snapshot && (snapshot.events.history_truncated || snapshot.events.gap);
  return (
    <div className="app-shell">
      <aside
        className={`sidebar ${mobile ? "is-open" : ""}`}
        inert={narrow && !mobile}
      >
        {mobile && (
          <button
            className="sidebar-close"
            aria-label="Close navigation"
            onClick={() => setMobile(false)}
          >
            <X size={17} />
          </button>
        )}
        <a
          className="brand"
          href="#view=overview"
          aria-label="Deadlock Sentinel home"
        >
          <span className="brand-icon">
            <Shield size={23} />
          </span>
          <span>
            Deadlock
            <span className="brand-second">
              Sentinel<span className="brand-period">.</span>
            </span>
          </span>
        </a>
        <div className="workspace">
          <span className="workspace-icon">
            <Blocks size={17} />
          </span>
          <div>
            Concurrency workspace<small>Local development</small>
          </div>
          <span className="tag">WSL</span>
        </div>
        <div className="nav-label">WORKSPACE</div>
        <nav aria-label="Main navigation">
          {pages.map(([key, label, Icon]) => (
            <button
              key={key}
              className={page === key ? "active" : ""}
              aria-current={page === key ? "page" : undefined}
              onClick={() => navigate(key)}
            >
              <Icon size={18} />
              <span>{label}</span>
              {key === "alerts" && !!s?.warning_count && (
                <span className="nav-count">{s.warning_count}</span>
              )}
              {page === key && <ChevronRight size={14} />}
            </button>
          ))}
        </nav>
        <div className="sidebar-bottom">
          <div className="engine-icon">
            <Activity size={19} />
          </div>
          <strong>Powered by real execution</strong>
          <p>
            Linux pthread interception.
            <br />C analyzer. Observable evidence.
          </p>
          <span className="version">
            PHASE 07 <span>LOCAL ONLY</span>
          </span>
        </div>
      </aside>
      <div className="main-shell">
        <header className="topbar">
          <div className="breadcrumb">
            <button
              className="menu-button"
              aria-label="Toggle navigation"
              aria-expanded={mobile}
              onClick={() => setMobile(!mobile)}
            >
              <Menu size={20} />
            </button>
            <span>Workspace</span>
            <ChevronRight size={13} />
            <strong>{title}</strong>
          </div>
          <div className="topbar-right">
            <span
              className={`connection ${backend === "Connected" ? "connected" : ""}`}
            >
              <span className="status-dot" />
              Backend {backend.toLowerCase()}
            </span>
            <span className="local-label">
              127.0.0.1 <span className="avatar">DS</span>
            </span>
          </div>
        </header>
        <main>
          <div className="page-heading">
            <div>
              <div className="eyebrow">CONCURRENCY OBSERVABILITY</div>
              <h1>
                {page === "overview"
                  ? "Understand every lock."
                  : replay
                    ? "Recorded Session Replay"
                    : title}
              </h1>
              <p>
                {page === "overview"
                  ? "Real-Time Concurrency Monitoring & Deadlock Risk Analysis"
                  : "Inspect real runtime evidence. Follow requests, dependencies and C-reported risks."}
              </p>
            </div>
            <button
              onClick={() => {
                void refresh();
                setRevision((n) => n + 1);
              }}
              disabled={loading}
            >
              <RefreshCw size={16} /> Refresh data
            </button>
          </div>
          <DemoHelp />
          {(error || live.error) && (
            <div className="error-banner" role="alert">
              <strong>Connection or operation needs attention</strong>
              <p>{error || live.error}</p>
              <button
                onClick={() => {
                  void refresh();
                  setRevision((n) => n + 1);
                }}
              >
                Retry connection
              </button>
            </div>
          )}
          {loading && !catalog.length && (
            <div className="loading" role="status">
              Connecting to the local monitoring backend…
            </div>
          )}
          {s && (
            <div className="session-strip">
              <div>
                <span className="eyebrow">SELECTED SESSION</span>
                <strong>{appName}</strong>
                <code>{s.session_id}</code>
              </div>
              <div className="actions">
                <Badge value={s.state} />
                <span className="stream-status">
                  <Wifi size={14} />
                  {live.connection}
                </span>
                {s.state === "created" && (
                  <button disabled={busy} onClick={() => void control("start")}>
                    Start created session
                  </button>
                )}
                {!terminal(s.state) && (
                  <button disabled={busy} onClick={() => void control("stop")}>
                    <Square size={13} /> Stop session
                  </button>
                )}
                {terminal(s.state) && s.event_count > 0 && (
                  <button onClick={() => navigate("sessions", id, true)}>
                    <History size={15} /> Replay
                  </button>
                )}
              </div>
            </div>
          )}
          {live.continuityLost && (
            <Notice>
              Stream continuity was interrupted. This view uses retained
              snapshots; delivery across disconnects is not guaranteed.
            </Notice>
          )}
          {incomplete && (
            <Notice>
              Retained history is truncated. Earlier events and historical
              states may be unavailable.
            </Notice>
          )}
          {s?.monitoring_complete === false && (
            <Notice>
              Monitoring is incomplete. C coverage:{" "}
              {String(s.c_monitor_complete)}; ingestion:{" "}
              {String(s.ingestion_complete)}. Missing records must not be
              treated as known state.
            </Notice>
          )}
          {s?.state === "failed" && (
            <Notice danger>
              Execution failed:{" "}
              {s.error || "See the session report for captured diagnostics."}
            </Notice>
          )}
          {s?.state === "timed_out" && (
            <Notice danger>
              {s.controlled_deadlock_evidence?.expected_timeout
                ? "Controlled expected timeout"
                : "Application timeout"}{" "}
              — actual outcome: timed_out. Cleanup{" "}
              {s.cleanup_verified ? "verified" : "not yet verified"}. C-reported
              cycle alerts are separate evidence.
            </Notice>
          )}
          <div className="stack">
            {(page === "overview" || page === "live") && (
              <>
                {snapshot ? (
                  <>
                    <div className="stats-grid">
                      {[
                        [
                          Activity,
                          "Session outcome",
                          s!.state,
                          `Duration ${duration}`,
                        ],
                        [
                          Users,
                          "Observed threads",
                          snapshot.threads.threads.length,
                          "Actual tracker instances",
                        ],
                        [
                          GitBranch,
                          "Discovered locks",
                          snapshot.graph.nodes.length,
                          `${snapshot.graph.edges.length} observed dependencies`,
                        ],
                        [
                          List,
                          "Monitored events",
                          s!.event_count,
                          `${s!.invalid_records} invalid records`,
                        ],
                        [
                          Bell,
                          "Potential cycles",
                          s!.warning_count,
                          "C-reported warnings",
                        ],
                      ].map(([Icon, label, value, sub]) => {
                        const MetricIcon = Icon as typeof Activity;
                        return (
                          <div className="stat-card" key={String(label)}>
                            <div>
                              <span>{String(label)}</span>
                              <MetricIcon size={18} />
                            </div>
                            <strong
                              className={
                                label === "Potential cycles" &&
                                Number(value) > 0
                                  ? "risk"
                                  : ""
                              }
                            >
                              {String(value).replaceAll("_", " ")}
                            </strong>
                            <small>{String(sub)}</small>
                          </div>
                        );
                      })}
                    </div>
                    <div className="observation-bar">
                      <Completeness value={s!.monitoring_complete} />
                      <span>
                        Historical dependencies · Last-observed thread state ·
                        No independent UI detection
                      </span>
                    </div>
                    <div className="monitor-grid">
                      <LockGraph graph={snapshot.graph} />
                      <Panel
                        title="Session intelligence"
                        subtitle="Evidence, with its limits"
                      >
                        <div className="intel-icon">
                          <Shield size={29} />
                        </div>
                        <h3>
                          {s!.warning_count
                            ? "Potential lock-order risk observed"
                            : "Follow the recorded execution"}
                        </h3>
                        <p>
                          {s!.warning_count
                            ? "The C analyzer reported a closed dependency path. Inspect its closing edge and the actual recorded event."
                            : "Requests, acquisitions and releases are captured directly from the monitored process."}
                        </p>
                        <button
                          className="text-button"
                          onClick={() =>
                            navigate(s!.warning_count ? "alerts" : "timeline")
                          }
                        >
                          {s!.warning_count
                            ? "Investigate warning"
                            : "Explore event timeline"}
                          <ArrowRight size={16} />
                        </button>
                        <hr />
                        <div className="key-value">
                          <span>Process ID</span>
                          <code>{s!.pid ?? "Not started"}</code>
                          <span>Exit code</span>
                          <code>{s!.exit_code ?? "Pending"}</code>
                          <span>Cleanup</span>
                          <span>
                            {s!.cleanup_verified ? "Verified" : "Pending"}
                          </span>
                        </div>
                        <div className="help-note">
                          <CircleHelp size={18} />
                          <p>
                            These programs finish quickly. Replay lets you study
                            each real event at your own pace.
                          </p>
                        </div>
                      </Panel>
                    </div>
                  </>
                ) : (
                  !loading && <Empty />
                )}
                <Launcher
                  applications={catalog}
                  busy={busy}
                  onStart={(app) => void start(app)}
                />
                {snapshot && page === "live" && (
                  <Timeline items={snapshot.events.items} />
                )}
              </>
            )}
            {page === "graph" &&
              (snapshot ? <LockGraph graph={snapshot.graph} /> : <Empty />)}
            {page === "threads" &&
              (snapshot ? <Threads data={snapshot.threads} /> : <Empty />)}
            {page === "alerts" &&
              (snapshot ? (
                <Alerts
                  data={snapshot.alerts}
                  sessionId={s!.session_id}
                  onGraph={() => navigate("graph")}
                />
              ) : (
                <Empty />
              ))}
            {page === "timeline" &&
              (snapshot ? (
                <Timeline items={snapshot.events.items} />
              ) : (
                <Empty />
              ))}
            {page === "sessions" && (
              <>
                <Sessions
                  sessions={sessions}
                  onSelect={(identifier) => navigate("live", identifier)}
                  onReplay={(identifier) =>
                    navigate("sessions", identifier, true)
                  }
                  onRefresh={() => void refresh()}
                />
                {replay &&
                  snapshot &&
                  (terminal(s!.state) ? (
                    <Replay key={s!.session_id} snapshot={snapshot} />
                  ) : (
                    <Notice>
                      Replay is available after execution ends. This session is
                      still active.
                    </Notice>
                  ))}
              </>
            )}
            {page === "reports" &&
              (snapshot ? (
                <Report
                  snapshot={{
                    ...snapshot,
                    client_continuity_lost: live.continuityLost,
                  }}
                  application={catalog.find(
                    (app) => app.application_id === s?.application_id,
                  )}
                />
              ) : (
                <Empty />
              ))}
            {page === "performance" && <Performance />}
          </div>
          <footer>
            <span>
              <Shield size={13} /> Deadlock Sentinel
            </span>
            <span>
              Real evidence. Explicit limits.{" "}
              <span className="subtle">
                Measured benchmarks in Performance.
              </span>
            </span>
          </footer>
        </main>
      </div>
    </div>
  );
}
