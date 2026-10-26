import { describe, it, expect, vi } from "vitest";
import { api, decode, request } from "../src/api/client";
import { applyMessage } from "../src/lib/stream";
import { reconstruct, reportedCycleEdges } from "../src/lib/replay";
import { graphElements } from "../src/components/LockGraph";
import { reportData } from "../src/components/Report";
import { snapshot, items, event } from "./fixtures";
describe("authoritative evidence processing", () => {
  it("marks all replay thread observations unknown after an interior sequence gap", () => {
    const history = [
      items[0],
      items[1],
      { ...items[3], event: { ...items[3].event, sequence: 8 } },
    ];
    const view = reconstruct(history, history.length);
    expect(view.graph.projection_complete).toBe(false);
    expect(view.threads.threads.every((t) => !t.observation_reliable)).toBe(
      true,
    );
  });
  it("carries a missing accepted-event index into replay and report history flags", () => {
    const state = applyMessage(
      { snapshot, cursor: 9, continuityLost: false },
      {
        type: "runtime_event",
        session_id: "unit-session",
        cursor: 11,
        data: { index: 8, event: event(8) },
      },
    );
    expect(state.snapshot?.events.gap).toBe(true);
    expect(reportData(state.snapshot!).events.gap).toBe(true);
  });
  it("preserves uint64 JSON integers exactly before number conversion", () => {
    expect(
      decode('{"timestamp_ns":18446744073709551615,"sequence":16}'),
    ).toEqual({ timestamp_ns: "18446744073709551615", sequence: 16 });
  });
  it("installs initial snapshots and ignores duplicate cursors", () => {
    const initial = applyMessage(
      { snapshot: null, cursor: -1, continuityLost: false },
      {
        type: "session_snapshot",
        session_id: "unit-session",
        cursor: 9,
        data: snapshot,
      },
    );
    const message = {
      type: "runtime_event" as const,
      session_id: "unit-session",
      cursor: 10,
      data: { index: 7, event: event(7) },
    };
    const next = applyMessage(initial, message);
    expect(next.snapshot?.events.items).toHaveLength(7);
    expect(applyMessage(next, message)).toBe(next);
  });
  it("detects cursor gaps and replaces history from reconnect snapshot", () => {
    const state = applyMessage(
      { snapshot, cursor: 9, continuityLost: false },
      {
        type: "runtime_event",
        session_id: "unit-session",
        cursor: 13,
        data: { index: 7, event: event(7) },
      },
    );
    expect(state.continuityLost).toBe(true);
    const recovered = applyMessage(state, {
      type: "session_snapshot",
      session_id: "unit-session",
      cursor: 14,
      data: snapshot,
    });
    expect(recovered.snapshot?.events.items).toHaveLength(6);
    expect(recovered.continuityLost).toBe(true);
  });
  it("never detects a warning from opposite graph edges alone", () => {
    const before = reconstruct(items, 4);
    expect(before.graph.edges).toHaveLength(2);
    expect(before.alerts.alerts).toHaveLength(0);
    expect(reconstruct(items, 5).alerts.alerts[0]).toEqual(items[4].event);
  });
  it("seeking is deterministic and previous state does not leak backward", () => {
    reconstruct(items, 6);
    expect(reconstruct(items, 2)).toEqual(reconstruct(items, 2));
    expect(reconstruct(items, 0).graph.nodes).toHaveLength(0);
    expect(reconstruct(items, 2).graph.edges).toHaveLength(1);
  });
  it("marks truncated replay unknown and does not guess missing dependencies", () => {
    const p = reconstruct(items.slice(3), 2, true);
    expect(p.graph.projection_complete).toBe(false);
    expect(p.graph.monitoring_complete).toBe(false);
    expect(p.graph.edges).toHaveLength(0);
    expect(p.alerts.alerts).toHaveLength(1);
  });
  it("honors recursive reentry and incomplete observations", () => {
    const recursive = {
      index: 1,
      event: event(1, {
        session_event: "lock_request",
        lock_id: "lock_1",
        held_locks: ["lock_1", "lock_2"],
        held_depths: [1, 1],
        lock_kind: "recursive",
      }),
    };
    expect(reconstruct([recursive], 1).graph.edges).toHaveLength(0);
    expect(
      reconstruct(
        [
          {
            ...recursive,
            event: {
              ...recursive.event,
              lock_kind: "normal",
              monitoring_complete: false,
            },
          },
        ],
        1,
      ).graph.edges,
    ).toHaveLength(0);
  });
  it("graph renders only observed nodes and edges, highlighting only reported paths", () => {
    const graph = graphElements(snapshot.graph);
    expect(graph.nodes.map((n) => n.id).sort()).toEqual(["lock_1", "lock_2"]);
    expect(graph.edges.map((e) => e.id).sort()).toEqual([
      "lock_1>lock_2",
      "lock_2>lock_1",
    ]);
    expect(graph.edges.every((e) => e.data.reportedCycle)).toBe(true);
    expect(reportedCycleEdges([]).size).toBe(0);
    expect(
      graphElements({ ...snapshot.graph, reported_cycles: [] }).edges.every(
        (e) => !e.data.reportedCycle,
      ),
    ).toBe(true);
  });
  it("exports original retained events and explicit limitations", () => {
    const report = reportData(snapshot);
    expect(report.events.items).toEqual(items);
    expect(report.session.stdout).toContain("UNIT FIXTURE");
    expect(report.limitations.join(" ")).toContain(
      "separate from this session",
    );
  });
});
describe("typed REST boundary", () => {
  it("creates then starts via the existing contract without arbitrary commands", async () => {
    const fetch = vi
      .spyOn(globalThis, "fetch")
      .mockResolvedValue(new Response(JSON.stringify(snapshot.session)));
    await api.create("banking_unsafe");
    expect(fetch.mock.calls[0][1]?.body).toBe(
      '{"application_id":"banking_unsafe","auto_start":false}',
    );
  });
  it("paginates retained history and preserves gap notices", async () => {
    vi.spyOn(globalThis, "fetch")
      .mockResolvedValueOnce(
        new Response(
          JSON.stringify({
            ...snapshot.events,
            items: items.slice(1, 3),
            next_after: 3,
            gap: true,
          }),
        ),
      )
      .mockResolvedValueOnce(
        new Response(
          JSON.stringify({
            ...snapshot.events,
            items: items.slice(3),
            next_after: 6,
          }),
        ),
      );
    const history = await api.history("unit-session");
    expect(history.items).toHaveLength(5);
    expect(history.gap).toBe(true);
  });
  it("surfaces unavailable backend and missing-session errors", async () => {
    vi.spyOn(globalThis, "fetch")
      .mockRejectedValueOnce(new Error("network"))
      .mockResolvedValueOnce(
        new Response('{"detail":"unknown_session"}', { status: 404 }),
      );
    await expect(request("/health")).rejects.toThrow("Backend unavailable");
    await expect(api.session("lost")).rejects.toThrow("backend restarts");
  });
});
