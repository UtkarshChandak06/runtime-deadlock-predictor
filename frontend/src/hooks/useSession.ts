import { useEffect, useState } from "react";
import { api, decode, socketUrl } from "../api/client";
import { terminal, type Envelope } from "../api/types";
import { applyMessage, type StreamState } from "../lib/stream";
export function useSession(
  id: string | null,
  autoStart: boolean,
  revision: number,
) {
  const [state, setState] = useState<StreamState>({
    snapshot: null,
    cursor: -1,
    continuityLost: false,
  });
  const [connection, setConnection] = useState("Idle");
  const [error, setError] = useState("");
  useEffect(() => {
    let alive = true,
      socket: WebSocket | undefined,
      retries = 0,
      started = false,
      refreshing = false;
    let retryTimer: ReturnType<typeof setTimeout>,
      refreshTimer: ReturnType<typeof setTimeout>;
    let current: StreamState = {
      snapshot: null,
      cursor: -1,
      continuityLost: false,
    };
    const abort = new AbortController();
    setState(current);
    setError("");
    setConnection(id ? "Loading session" : "Idle");
    if (!id)
      return () => {
        alive = false;
        abort.abort();
      };
    const commit = (next: StreamState) => {
      current = next;
      if (alive) setState(next);
    };
    async function projections() {
      if (!alive || refreshing) return;
      refreshing = true;
      const cursor = current.cursor;
      try {
        const [session, graph, threads, alerts] = await Promise.all([
          api.session(id!, abort.signal),
          api.graph(id!, abort.signal),
          api.threads(id!, abort.signal),
          api.alerts(id!, abort.signal),
        ]);
        if (alive && current.snapshot && cursor === current.cursor)
          commit({
            ...current,
            snapshot: { ...current.snapshot, session, graph, threads, alerts },
          });
        else if (alive) refreshTimer = setTimeout(projections, 120);
      } catch (e) {
        if (alive) setError((e as Error).message);
      } finally {
        refreshing = false;
      }
    }
    const refresh = () => {
      clearTimeout(refreshTimer);
      refreshTimer = setTimeout(projections, 80);
    };
    async function connect() {
      if (!alive) return;
      try {
        const baseline = await api.snapshot(id!, abort.signal);
        if (!alive) return;
        commit({ ...current, snapshot: baseline });
        setConnection(retries ? "Reconnecting" : "Connecting");
        socket = new WebSocket(socketUrl(id!));
        socket.onmessage = async (event) => {
          if (!alive) return;
          try {
            const message = decode<Envelope>(event.data);
            if (message.session_id !== id) return;
            commit(applyMessage(current, message));
            if (message.type === "session_snapshot") {
              setConnection(
                terminal(message.data.session.state)
                  ? "Recorded session"
                  : "Live stream",
              );
              setError("");
              if (
                autoStart &&
                !started &&
                message.data.session.state === "created"
              ) {
                started = true;
                try {
                  await api.start(id!);
                } catch (e) {
                  if (alive) setError((e as Error).message);
                }
              }
            }
            if (
              message.type === "runtime_event" ||
              message.type === "session_status"
            )
              refresh();
            if (
              message.type === "monitoring_error" &&
              message.data.code === "slow_consumer"
            )
              commit({ ...current, continuityLost: true });
          } catch (e) {
            if (alive)
              setError(
                "Stream message could not be processed: " +
                  (e as Error).message,
              );
          }
        };
        socket.onerror = () => {
          if (alive) setConnection("Connection interrupted");
        };
        socket.onclose = async () => {
          if (!alive) return;
          if (terminal(current.snapshot?.session.state)) {
            try {
              const retained = await api.snapshot(id!, abort.signal);
              if (alive) {
                commit({ ...current, snapshot: retained });
                setConnection("Recorded session");
              }
            } catch (e) {
              if (alive) {
                setError(
                  "Could not reload terminal history: " + (e as Error).message,
                );
                setConnection("Disconnected");
                commit({ ...current, continuityLost: true });
              }
            }
            return;
          }
          commit({ ...current, continuityLost: true });
          if (retries++ < 4) {
            setConnection(`Reconnecting ${retries}/4`);
            retryTimer = setTimeout(
              connect,
              Math.min(500 * 2 ** (retries - 1), 4000),
            );
          } else {
            setConnection("Disconnected");
            setError(
              "Live connection unavailable. Retry to reload retained evidence. No continuous delivery is guaranteed.",
            );
            void projections();
          }
        };
      } catch (e) {
        if (alive) {
          setConnection("Unavailable");
          setError((e as Error).message);
        }
      }
    }
    void connect();
    return () => {
      alive = false;
      abort.abort();
      clearTimeout(retryTimer);
      clearTimeout(refreshTimer);
      if (socket) {
        socket.onclose = null;
        socket.onmessage = null;
        socket.close();
      }
    };
  }, [id, autoStart, revision]);
  return { ...state, connection, error };
}
