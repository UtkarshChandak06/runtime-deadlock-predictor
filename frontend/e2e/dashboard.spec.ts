import { test, expect } from "@playwright/test";
import fs from "node:fs";
test("real C → FastAPI → browser: safe, unsafe, replay, bounded deadlock and reports", async ({
  page,
  request,
}) => {
  const errors: string[] = [];
  page.on("pageerror", (error) => errors.push(error.message));
  const messages: { type: string; data: unknown }[] = [];
  page.on("websocket", (socket) =>
    socket.on("framereceived", (frame) => {
      try {
        messages.push(JSON.parse(String(frame.payload)));
      } catch {
        /* Vite HMR messages are not monitor records. */
      }
    }),
  );
  await page.goto("/");
  await expect(
    page.getByRole("button", { name: "Start monitoring Safe banking" }),
  ).toBeEnabled();
  await page.screenshot({
    path: "test-results/overview-empty.png",
    fullPage: true,
  });
  await page.locator(".demo-help summary").click();
  await page.screenshot({path:"test-results/beginner-help.png",fullPage:true});
  await page.locator(".demo-help summary").click();
  await page
    .getByRole("button", { name: "Start monitoring Safe banking" })
    .click();
  await expect(page.locator(".session-strip .badge")).toHaveText("completed");
  await expect(
    page
      .locator(".stat-card")
      .filter({ hasText: "Monitored events" })
      .locator("strong"),
  ).toHaveText("22");
  await expect(page.locator(".react-flow__node")).toHaveCount(2);
  await expect(page.locator(".react-flow__edge")).toHaveCount(1);
  await expect(
    page
      .locator(".stat-card")
      .filter({ hasText: "Potential cycles" })
      .locator("strong"),
  ).toHaveText("0");
  const safeId = new URLSearchParams(new URL(page.url()).hash.slice(1)).get(
    "session",
  )!;
  const safe = await (
    await request.get(`http://127.0.0.1:8000/api/v1/sessions/${safeId}`)
  ).json();
  expect(safe.cleanup_verified).toBe(true);
  await page.screenshot({
    path: "test-results/safe-completed.png",
    fullPage: true,
  });
  await page
    .getByRole("navigation")
    .getByRole("button", { name: "Alerts", exact: true })
    .click();
  await expect(
    page.getByText("No potential-cycle warnings in this view"),
  ).toBeVisible();
  await page
    .getByRole("navigation")
    .getByRole("button", { name: "Overview", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Start monitoring Unsafe lock ordering" })
    .click();
  await expect(page.locator(".session-strip .badge")).toHaveText("completed");
  await expect(
    page
      .locator(".stat-card")
      .filter({ hasText: "Potential cycles" })
      .locator("strong"),
  ).toHaveText("1");
  await expect(page.locator(".react-flow__edge")).toHaveCount(2);
  await expect(page.locator(".cycle-node")).toHaveCount(2);
  const unsafeId = new URLSearchParams(new URL(page.url()).hash.slice(1)).get(
    "session",
  )!;
  const alert = await (
    await request.get(
      `http://127.0.0.1:8000/api/v1/sessions/${unsafeId}/alerts`,
    )
  ).json();
  expect(alert.alerts[0].cycle).toEqual(["lock_1", "lock_2", "lock_1"]);
  await page.screenshot({
    path: "test-results/unsafe-live-1366.png",
    fullPage: true,
  });
  await page.getByRole("navigation").getByRole("button",{name:"Live Monitor",exact:true}).click();
  await page.screenshot({path:"test-results/live-monitor.png",fullPage:true});
  await page
    .getByRole("navigation")
    .getByRole("button", { name: "Alerts", exact: false })
    .click();
  await expect(
    page.getByText("Potential Lock-Order Cycle", { exact: true }),
  ).toBeVisible();
  await expect(page.locator(".cycle-path")).toHaveText(
    alert.alerts[0].cycle.join(" → "),
  );
  await page.screenshot({ path: "test-results/alerts.png", fullPage: true });
  await page
    .getByRole("button", { name: "Inspect reported cycle in graph" })
    .click();
  await expect(
    page.locator(".react-flow__edge path.react-flow__edge-path").first(),
  ).toHaveCSS("stroke", "rgb(251, 113, 133)");
  await page.locator(".react-flow__node").first().click();
  await expect(page.getByText("Selected graph evidence")).toBeVisible();
  await page.screenshot({ path: "test-results/graph.png", fullPage: true });
  await page
    .getByRole("navigation")
    .getByRole("button", { name: "Thread Explorer" })
    .click();
  await page.locator(".thread-card").first().click();
  await expect(
    page.getByRole("button", { name: "Close thread" }),
  ).toBeVisible();
  await page.screenshot({ path: "test-results/threads.png", fullPage: true });
  await page
    .getByRole("navigation")
    .getByRole("button", { name: "Event Timeline" })
    .click();
  await page
    .getByLabel("Event type")
    .selectOption("potential_lock_order_cycle");
  await expect(page.getByText(/1 matching events/)).toBeVisible();
  await page.screenshot({ path: "test-results/timeline.png", fullPage: true });
  await page.getByRole("button", { name: "Replay", exact: true }).click();
  const sessionsBeforeReplay = await (await request.get("http://127.0.0.1:8000/api/v1/sessions")).json();
  await expect(
    page.getByText("Recorded Session Replay", { exact: true }).last(),
  ).toBeVisible();
  await page.getByLabel("Replay position").fill("15");
  await expect(
    page.getByText("Potential Lock-Order Cycle", { exact: true }),
  ).toHaveCount(0);
  await page.getByLabel("Next event", { exact: true }).click();
  await expect(
    page.getByText("Potential Lock-Order Cycle", { exact: true }),
  ).toBeVisible();
  await expect(page.locator(".react-flow__edge")).toHaveCount(2);
  await page.getByRole("button", { name: "Play", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Pause", exact: true }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Pause", exact: true }).click();
  expect(await (await request.get("http://127.0.0.1:8000/api/v1/sessions")).json()).toEqual(sessionsBeforeReplay);
  await page.screenshot({
    path: "test-results/unsafe-replay.png",
    fullPage: true,
  });
  await page
    .getByRole("navigation")
    .getByRole("button", { name: "Reports" })
    .click();
  const downloadPromise = page.waitForEvent("download");
  await page.getByRole("button", { name: "Export JSON" }).click();
  const download = await downloadPromise;
  const exported = JSON.parse(
    fs.readFileSync((await download.path())!, "utf8"),
  );
  expect(exported.session.session_id).toBe(unsafeId);
  expect(exported.alerts.alerts).toEqual(alert.alerts);
  expect(exported.events.items).toHaveLength(23);
  expect(exported.application).not.toBeNull();
  await page.screenshot({ path: "test-results/reports.png", fullPage: true });
  await page
    .getByRole("navigation")
    .getByRole("button", { name: "Performance", exact: true })
    .click();
  await expect(page.getByText("144 raw runs")).toBeVisible();
  await expect(page.locator("tbody tr")).toHaveCount(16);
  await page.screenshot({
    path: "test-results/performance.png",
    fullPage: true,
  });
  await page
    .getByRole("navigation")
    .getByRole("button", { name: "Overview", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Start monitoring Controlled deadlock" })
    .click();
  await expect(page.locator(".session-strip .badge")).toHaveText("timed out", {
    timeout: 15000,
  });
  await expect(page.getByText(/Controlled expected timeout/)).toBeVisible();
  const deadId = new URLSearchParams(new URL(page.url()).hash.slice(1)).get(
    "session",
  )!;
  const dead = await (
    await request.get(`http://127.0.0.1:8000/api/v1/sessions/${deadId}`)
  ).json();
  expect(dead.cleanup_verified).toBe(true);
  expect(dead.controlled_deadlock_evidence.fixture_verified).toBe(true);
  await expect(
    page
      .locator(".stat-card")
      .filter({ hasText: "Potential cycles" })
      .locator("strong"),
  ).toHaveText("1");
  await page.screenshot({
    path: "test-results/deadlock-timeout.png",
    fullPage: true,
  });
  await page.setViewportSize({ width: 1920, height: 1080 });
  await page.screenshot({
    path: "test-results/desktop-1920.png",
    fullPage: true,
  });
  await page.getByRole("navigation").getByRole("button",{name:"Sessions & Replay",exact:true}).click();
  await page.screenshot({path:"test-results/sessions-desktop.png",fullPage:true});
  await page.setViewportSize({ width: 390, height: 844 });
  expect(
    await page.evaluate(
      () => document.documentElement.scrollWidth <= innerWidth,
    ),
  ).toBe(true);
  await page.getByRole("button", { name: "Toggle navigation" }).click();
  await page
    .getByRole("navigation")
    .getByRole("button", { name: "Sessions & Replay" })
    .click();
  await expect(page.getByText("Retained sessions")).toBeVisible();
  await page.screenshot({
    path: "test-results/mobile-390.png",
    fullPage: true,
  });
  expect(messages.some((m) => m.type === "runtime_event")).toBe(true);
  expect(errors).toEqual([]);
  fs.writeFileSync(
    "test-results/real-session-evidence.json",
    JSON.stringify(
      {
        safe,
        unsafeReport: exported,
        controlledDeadlock: dead,
        monitorWebSocketFrames: messages.filter((m) =>
          [
            "runtime_event",
            "session_snapshot",
            "session_status",
            "monitoring_error",
          ].includes(m.type),
        ).length,
        browserErrors: errors,
      },
      null,
      2,
    ),
  );
});
test("unavailable backend and missing session show honest error states", async ({
  page,
}) => {
  await page.goto("/#view=live&session=does-not-exist");
  await expect(page.getByRole("alert")).toContainText("not found");
  await page.route("http://127.0.0.1:8000/**", (route) => route.abort());
  await page.goto("/#view=overview");
  await page.reload();
  await expect(page.getByRole("alert")).toContainText("Backend unavailable");
  await expect(page.locator(".stat-card")).toHaveCount(0);
});
test("real session survives browser disconnect, reconnects, and UI stop reaps its child", async ({
  page,
  request,
}) => {
  await page.addInitScript(() => {
    const sockets: WebSocket[] = [];
    (window as unknown as { testSockets: WebSocket[] }).testSockets = sockets;
    const Native = window.WebSocket;
    window.WebSocket = class extends Native {
      constructor(url: string | URL, protocols?: string | string[]) {
        super(url, protocols);
        sockets.push(this);
      }
    };
  });
  await page.goto("/");
  await page
    .getByRole("button", { name: "Start monitoring Controlled deadlock" })
    .click();
  await expect(page.locator(".session-strip .badge")).toHaveText("running");
  await page.evaluate(() => {
    const sockets = (window as unknown as { testSockets: WebSocket[] })
      .testSockets;
    for (const socket of sockets)
      if (socket.url.includes("/ws/v1/sessions/"))
        socket.close(4000, "test disconnect");
  });
  await expect(
    page.getByText(/Stream continuity was interrupted/),
  ).toBeVisible();
  await expect(page.locator(".session-strip .badge")).toHaveText("timed out", {
    timeout: 15000,
  });
  await expect(
    page.getByText("Recorded session", { exact: true }),
  ).toBeVisible();
  await page
    .getByRole("button", { name: "Start monitoring Controlled deadlock" })
    .click();
  await expect(page.locator(".session-strip .badge")).toHaveText("running");
  await page.getByRole("button", { name: "Stop session" }).click();
  await expect(page.locator(".session-strip .badge")).toHaveText("stopped");
  const id = new URLSearchParams(new URL(page.url()).hash.slice(1)).get(
    "session",
  )!;
  const session = await (
    await request.get(`http://127.0.0.1:8000/api/v1/sessions/${id}`)
  ).json();
  expect(session.cleanup_verified).toBe(true);
  await page.reload();
  await expect(page.locator(".session-strip .badge")).toHaveText("stopped");
  await expect(
    page.getByText("Recorded session", { exact: true }),
  ).toBeVisible();
});
