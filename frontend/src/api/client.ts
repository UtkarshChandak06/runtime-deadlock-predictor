import { parse, isLosslessNumber } from "lossless-json";
import type {
  Application,
  Session,
  Graph,
  Threads,
  Alerts,
  Snapshot,
  EventPage,
} from "./types";
export const BASE = (
  import.meta.env.VITE_API_BASE_URL || "http://127.0.0.1:8000"
).replace(/\/$/, "");
// Parse before Number conversion: uint64 timestamps/identities must never round.
export function decode<T>(text: string): T {
  return parse(text, (_key, value) => {
    if (!isLosslessNumber(value)) return value;
    const n = Number(value.value);
    return Number.isSafeInteger(n) ? n : value.value;
  }) as T;
}
export const json = (value: unknown) => JSON.stringify(value, null, 2);
export class ApiError extends Error {
  constructor(
    public status: number,
    message: string,
  ) {
    super(message);
  }
}
export async function request<T>(
  path: string,
  options: RequestInit = {},
): Promise<T> {
  let response: Response;
  try {
    response = await fetch(BASE + "/api/v1" + path, {
      ...options,
      headers: { "Content-Type": "application/json", ...options.headers },
    });
  } catch (error) {
    if ((error as Error).name === "AbortError") throw error;
    throw new ApiError(
      0,
      "Backend unavailable. Start make backend-run in Ubuntu/WSL, then retry.",
    );
  }
  const data = decode<T & { detail?: unknown }>(await response.text());
  if (!response.ok)
    throw new ApiError(
      response.status,
      response.status === 404
        ? "Session or application not found. Sessions are lost when the backend restarts."
        : `Request failed (${response.status}): ${typeof data.detail === "string" ? data.detail : JSON.stringify(data.detail)}`,
    );
  return data;
}
export const api = {
  applications: (signal?: AbortSignal) =>
    request<Application[]>("/applications", { signal }),
  sessions: (signal?: AbortSignal) =>
    request<Session[]>("/sessions", { signal }),
  create: (application_id: string) =>
    request<Session>("/sessions", {
      method: "POST",
      body: JSON.stringify({ application_id, auto_start: false }),
    }),
  start: (id: string) =>
    request<Session>(`/sessions/${encodeURIComponent(id)}/start`, {
      method: "POST",
    }),
  stop: (id: string) =>
    request<Session>(`/sessions/${encodeURIComponent(id)}/stop`, {
      method: "POST",
    }),
  session: (id: string, signal?: AbortSignal) =>
    request<Session>(`/sessions/${encodeURIComponent(id)}`, { signal }),
  graph: (id: string, signal?: AbortSignal) =>
    request<Graph>(`/sessions/${encodeURIComponent(id)}/graph`, { signal }),
  threads: (id: string, signal?: AbortSignal) =>
    request<Threads>(`/sessions/${encodeURIComponent(id)}/threads`, { signal }),
  alerts: (id: string, signal?: AbortSignal) =>
    request<Alerts>(`/sessions/${encodeURIComponent(id)}/alerts`, { signal }),
  async history(id: string, signal?: AbortSignal): Promise<EventPage> {
    let after = 0,
      items: EventPage["items"] = [],
      gap = false,
      truncated = false,
      page: EventPage;
    // Backend maximum retention is 10,000. Never grow history indefinitely.
    do {
      page = await request<EventPage>(
        `/sessions/${encodeURIComponent(id)}/events?after=${after}&limit=1000`,
        { signal },
      );
      items.push(...page.items);
      gap ||= page.gap;
      truncated ||= page.history_truncated;
      if (!page.items.length || page.next_after <= after) break;
      after = page.next_after;
    } while (after < page.total_received && items.length < 10000);
    const unique = [
      ...new Map(items.map((item) => [item.index, item])).values(),
    ].slice(-10000);
    return {
      ...page!,
      items: unique,
      next_after: after,
      gap,
      history_truncated: truncated || items.length > 10000,
    };
  },
  async snapshot(id: string, signal?: AbortSignal): Promise<Snapshot> {
    const [session, events, graph, threads, alerts] = await Promise.all([
      api.session(id, signal),
      api.history(id, signal),
      api.graph(id, signal),
      api.threads(id, signal),
      api.alerts(id, signal),
    ]);
    // REST resources may be sampled at different moments while running; WS replaces this baseline.
    return { session, events, graph, threads, alerts, errors: [] };
  },
};
export const socketUrl = (id: string) =>
  BASE.replace(/^http/, "ws") + "/ws/v1/sessions/" + encodeURIComponent(id);
