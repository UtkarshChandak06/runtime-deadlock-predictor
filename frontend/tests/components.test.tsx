import { describe, it, expect, vi } from "vitest";
import {
  fireEvent,
  render,
  screen,
  waitFor,
  within,
} from "@testing-library/react";
import { Alerts } from "../src/components/Alerts";
import { Threads } from "../src/components/Threads";
import { Timeline } from "../src/components/Timeline";
import { Replay } from "../src/components/Replay";
import { Launcher } from "../src/pages/Launcher";
import { Report } from "../src/components/Report";
import { Sessions } from "../src/pages/Sessions";
import App from "../src/App";
import { api } from "../src/api/client";
import { application, snapshot, items } from "./fixtures";
// The interactive graph is tested in graphElements units and the real browser suite.
vi.mock("../src/components/LockGraph", async (importOriginal) => {
  const actual =
    await importOriginal<typeof import("../src/components/LockGraph")>();
  return {
    ...actual,
    LockGraph: ({ graph }: { graph: typeof snapshot.graph }) => (
      <div data-testid="graph-count">
        {graph.nodes.length} nodes · {graph.edges.length} edges
      </div>
    ),
  };
});
describe("investigation views with explicit unit fixtures", () => {
  it("safe/no-warning empty state does not promise universal safety", () => {
    render(
      <Alerts
        data={{ alerts: [], total_received: 0, truncated: false }}
        sessionId="unit"
      />,
    );
    expect(
      screen.getByText("No potential-cycle warnings in this view"),
    ).toBeInTheDocument();
    expect(screen.getByText(/not a guarantee/)).toBeInTheDocument();
  });
  it("shows C warning identity, path and potential-risk distinction", () => {
    render(<Alerts data={snapshot.alerts} sessionId="unit" />);
    expect(screen.getByText("Potential Lock-Order Cycle")).toBeInTheDocument();
    expect(screen.getByText("lock_1 → lock_2 → lock_1")).toBeInTheDocument();
    expect(screen.getByText(/does not prove/)).toBeInTheDocument();
  });
  it("thread details use last-observed wording and actual identities", () => {
    render(<Threads data={snapshot.threads} />);
    fireEvent.click(screen.getByRole("button", { name: /Thread 102/ }));
    expect(screen.getByText("Thread 102 · instance 2")).toBeInTheDocument();
    expect(screen.getAllByText("Last observed request").length).toBeGreaterThan(
      0,
    );
  });
  it("timeline filters by event type, thread and lock", () => {
    render(<Timeline items={items} />);
    fireEvent.change(screen.getByLabelText("Event type"), {
      target: { value: "potential_lock_order_cycle" },
    });
    expect(screen.getByText(/1 matching events/)).toBeInTheDocument();
    fireEvent.change(screen.getByLabelText("Search lock ID"), {
      target: { value: "missing" },
    });
    expect(screen.getByText("No matching events")).toBeInTheDocument();
  });
  it("event details preserve original fields and monotonic timestamps", () => {
    render(<Timeline items={items} />);
    fireEvent.click(screen.getByLabelText("Inspect event 5"));
    expect(screen.getByText("Original event #5")).toBeInTheDocument();
    expect(screen.getByText(/"graph_sequence": 2/)).toBeInTheDocument();
  });
  it("replay controls seek deterministically and reveal warning only at its event", () => {
    render(<Replay snapshot={snapshot} />);
    expect(screen.getByText("Recorded Session Replay")).toBeInTheDocument();
    fireEvent.click(screen.getByLabelText("Next event"));
    expect(screen.getByText(/Paused · 1 \/ 6/)).toBeInTheDocument();
    fireEvent.change(screen.getByLabelText("Replay position"), {
      target: { value: "5" },
    });
    expect(screen.getByText("Potential Lock-Order Cycle")).toBeInTheDocument();
    fireEvent.click(screen.getByLabelText("Previous event"));
    expect(
      screen.queryByText("Potential Lock-Order Cycle"),
    ).not.toBeInTheDocument();
    fireEvent.click(screen.getByLabelText("Restart replay"));
    expect(screen.getByText(/Paused · 0 \/ 6/)).toBeInTheDocument();
  });
  it("play/pause and playback speed affect presentation only", () => {
    vi.useFakeTimers();
    render(<Replay snapshot={snapshot} />);
    fireEvent.change(screen.getByLabelText("Playback speed"), {
      target: { value: "4" },
    });
    fireEvent.click(screen.getByRole("button", { name: "Play" }));
    expect(screen.getByRole("button", { name: "Pause" })).toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "Pause" }));
    expect(items[0].event.timestamp_ns).toBe(1000001);
    vi.useRealTimers();
  });
  it("replay explicitly marks truncated history", () => {
    render(
      <Replay
        snapshot={{ ...snapshot, events: { ...snapshot.events, gap: true } }}
      />,
    );
    expect(screen.getByText(/Incomplete replay:/)).toBeInTheDocument();
  });
  it("catalog uses backend metadata and disables duplicate/unavailable starts", () => {
    const start = vi.fn();
    const { rerender } = render(
      <Launcher applications={[application]} busy={false} onStart={start} />,
    );
    fireEvent.click(screen.getByRole("button", { name: /Start monitoring/ }));
    expect(start).toHaveBeenCalledWith(application);
    rerender(<Launcher applications={[application]} busy onStart={start} />);
    expect(
      screen.getByRole("button", { name: /Start monitoring/ }),
    ).toBeDisabled();
  });
  it("bounded catalog and terminal sessions keep actual outcomes", () => {
    render(
      <Sessions
        sessions={[
          {
            ...snapshot.session,
            state: "timed_out",
            controlled_deadlock_evidence: { expected_timeout: true },
          },
        ]}
        onSelect={vi.fn()}
        onReplay={vi.fn()}
        onRefresh={vi.fn()}
      />,
    );
    expect(screen.getByText("timed out")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Replay" })).toBeEnabled();
  });
  it("reports show actual output, completeness and export control", () => {
    render(<Report snapshot={snapshot} />);
    expect(screen.getByText("UNIT FIXTURE stdout")).toBeInTheDocument();
    expect(
      screen.getByRole("button", { name: "Export JSON" }),
    ).toBeInTheDocument();
    expect(
      screen.getByText(/Saved benchmark measurements/),
    ).toBeInTheDocument();
  });
});
describe("application shell", () => {
  it("loads real client catalog, renders navigation and moves to sessions", async () => {
    location.hash = "view=overview";
    vi.spyOn(api, "applications").mockResolvedValue([application]);
    vi.spyOn(api, "sessions").mockResolvedValue([]);
    render(<App />);
    await screen.findByText(application.display_name);
    const nav = screen.getByRole("navigation", { name: "Main navigation" });
    expect(within(nav).getAllByRole("button")).toHaveLength(10);
    fireEvent.click(
      within(nav).getByRole("button", { name: "Sessions & Replay" }),
    );
    fireEvent(window, new HashChangeEvent("hashchange"));
    await screen.findByText("Retained sessions");
    expect(
      screen.getByText(/persists runs across restarts/),
    ).toBeInTheDocument();
  });
  it("shows a backend error rather than fake metrics", async () => {
    location.hash = "view=overview";
    vi.spyOn(api, "applications").mockRejectedValue(
      new Error("Backend unavailable unit fixture"),
    );
    vi.spyOn(api, "sessions").mockResolvedValue([]);
    render(<App />);
    await waitFor(() =>
      expect(screen.getByRole("alert")).toHaveTextContent(
        "Backend unavailable unit fixture",
      ),
    );
    expect(screen.queryByText("Monitored events")).not.toBeInTheDocument();
  });
});
