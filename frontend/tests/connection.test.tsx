import { act, renderHook, waitFor } from "@testing-library/react";
import { afterEach, expect, it, vi } from "vitest";
import { useSession } from "../src/hooks/useSession";
import { api } from "../src/api/client";
import { snapshot } from "./fixtures";
class FixtureSocket {
  static sockets: FixtureSocket[] = [];
  onmessage: ((event: { data: string }) => void) | null = null;
  onclose: (() => void) | null = null;
  onerror: (() => void) | null = null;
  closed = false;
  constructor(public url: string) {
    FixtureSocket.sockets.push(this);
  }
  close() {
    this.closed = true;
  }
  message(value: unknown) {
    this.onmessage?.({ data: JSON.stringify(value) });
  }
}
afterEach(() => {
  vi.unstubAllGlobals();
  FixtureSocket.sockets = [];
});
it("reloads retained terminal history after a stream gap while preserving the interruption warning", async () => {
  vi.stubGlobal("WebSocket", FixtureSocket);
  const reload = vi.spyOn(api, "snapshot").mockResolvedValue(snapshot);
  const { result, unmount } = renderHook(() =>
    useSession("unit-session", false, 0),
  );
  await waitFor(() => expect(FixtureSocket.sockets).toHaveLength(1));
  await act(async () => {
    FixtureSocket.sockets[0].message({
      type: "monitoring_error",
      session_id: "unit-session",
      cursor: 10,
      data: { code: "slow_consumer", message: "test gap" },
    });
    FixtureSocket.sockets[0].onclose?.();
  });
  await waitFor(() => expect(reload).toHaveBeenCalledTimes(2));
  expect(result.current.snapshot?.events.items).toEqual(snapshot.events.items);
  expect(result.current.continuityLost).toBe(true);
  expect(result.current.connection).toBe("Recorded session");
  unmount();
});
it("connects before starting, consumes snapshot once, and closes subscriptions on unmount", async () => {
  vi.stubGlobal("WebSocket", FixtureSocket);
  const created = {
    ...snapshot,
    session: { ...snapshot.session, state: "created" as const },
  };
  vi.spyOn(api, "snapshot").mockResolvedValue(created);
  const start = vi.spyOn(api, "start").mockResolvedValue(snapshot.session);
  const { result, unmount } = renderHook(() =>
    useSession("unit-session", true, 0),
  );
  await waitFor(() => expect(FixtureSocket.sockets).toHaveLength(1));
  expect(start).not.toHaveBeenCalled();
  const ws = FixtureSocket.sockets[0];
  await act(async () => {
    ws.message({
      type: "session_snapshot",
      session_id: "unit-session",
      cursor: 9,
      data: created,
    });
    ws.message({
      type: "session_snapshot",
      session_id: "unit-session",
      cursor: 9,
      data: created,
    });
  });
  expect(start).toHaveBeenCalledTimes(1);
  expect(result.current.snapshot?.events.items).toHaveLength(6);
  unmount();
  expect(ws.closed).toBe(true);
  expect(ws.onmessage).toBeNull();
});
it("switching sessions aborts old subscriptions and rejects stale identities", async () => {
  vi.stubGlobal("WebSocket", FixtureSocket);
  vi.spyOn(api, "snapshot").mockResolvedValue(snapshot);
  const { result, rerender, unmount } = renderHook(
    ({ id }) => useSession(id, false, 0),
    { initialProps: { id: "unit-session" } },
  );
  await waitFor(() => expect(FixtureSocket.sockets).toHaveLength(1));
  const first = FixtureSocket.sockets[0];
  rerender({ id: "new-session" });
  await waitFor(() => expect(FixtureSocket.sockets).toHaveLength(2));
  expect(first.closed).toBe(true);
  await act(async () =>
    FixtureSocket.sockets[1].message({
      type: "session_snapshot",
      session_id: "old-session",
      cursor: 300,
      data: snapshot,
    }),
  );
  expect(result.current.cursor).toBe(-1);
  unmount();
});
