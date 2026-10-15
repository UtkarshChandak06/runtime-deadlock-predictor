const scenarios = {
  safe: [
    ["Ready to observe", "No lock-order dependencies have been recorded.", false, false],
    ["Thread 1 holds A", "The first lock creates no dependency because the thread held nothing before A.", false, false],
    ["Thread 1 requests B", "The monitor records A → B. No path returns from B to A, so there is no cycle.", true, false],
    ["Thread 2 repeats A → B", "The same dependency is already known. Duplicate edges do not create a warning.", true, false],
    ["Sequence completes safely", "Both threads used a consistent order. The graph remains acyclic.", true, false]
  ],
  danger: [
    ["Ready to observe", "No lock-order dependencies have been recorded.", false, false],
    ["Thread 1 holds A", "The first lock creates no dependency because the thread held nothing before A.", false, false],
    ["Thread 1 requests B", "The monitor records A → B. The graph is still acyclic.", true, false],
    ["Thread 2 holds B", "The second thread now owns B and prepares to request A.", true, false],
    ["Thread 2 requests A", "Adding B → A closes the complete cycle A → B → A. A potential deadlock warning is emitted.", true, true]
  ]
};

let scenario = "safe";
let step = 0;
const $ = (selector) => document.querySelector(selector);

function renderDemo() {
  const data = scenarios[scenario][step];
  const isDanger = data[3];
  $("#scenario-title").textContent = scenario === "safe" ? "Safe order" : "Dangerous order";
  $("#thread-two").textContent = scenario === "safe" ? "lock(A) → lock(B)" : "lock(B) → lock(A)";
  $("#step-pill").textContent = `Step ${step} / 4`;
  $("#step-number").textContent = String(step).padStart(2, "0");
  $("#step-title").textContent = data[0];
  $("#step-description").textContent = data[1];
  $("#edge-ab").classList.toggle("visible", data[2]);
  $("#edge-ba").classList.toggle("visible", isDanger);
  $("#node-a").classList.toggle("active", data[2] && !isDanger);
  $("#node-b").classList.toggle("active", data[2] && !isDanger);
  $("#node-a").classList.toggle("danger", isDanger);
  $("#node-b").classList.toggle("danger", isDanger);
  const status = $("#graph-status");
  status.className = `graph-status ${isDanger ? "danger" : "safe"}`;
  status.innerHTML = isDanger
    ? '<span class="status-icon">!</span><div><strong>Potential deadlock cycle</strong><small>A → B → A closes the graph cycle.</small></div>'
    : '<span class="status-icon">✓</span><div><strong>No cycle found</strong><small>The observed order is currently safe.</small></div>';
  $("#demoGraphDesc").textContent = isDanger ? "A red cycle connects mutex A to B and returns from B to A." : data[2] ? "A dependency connects mutex A to mutex B." : "No dependencies recorded.";
  $("#next-step").disabled = step === 4;
  $("#next-step").innerHTML = step === 4 ? "Sequence complete" : "Next step <span aria-hidden=\"true\">→</span>";
  $("#terminal-output").innerHTML = isDanger
    ? '<span class="muted">$ LD_PRELOAD=./build/libdeadlock_monitor.so ./program</span>\n[WARNING] Potential Deadlock Detected (not a confirmed deadlock)\nThread: tid=4821\nHeld Lock: 0x7ffd...B\nRequested Lock: 0x7ffd...A\nCycle: 0x7ffd...A → 0x7ffd...B → 0x7ffd...A'
    : '<span class="muted">$ LD_PRELOAD=./build/libdeadlock_monitor.so ./program</span>\n<span class="quiet">[monitor] quiet — no cycle has been observed</span>';
}

document.querySelectorAll(".tab").forEach((button) => button.addEventListener("click", () => {
  scenario = button.dataset.scenario;
  step = 0;
  document.querySelectorAll(".tab").forEach((item) => {
    const active = item === button;
    item.classList.toggle("active", active);
    item.setAttribute("aria-pressed", active);
  });
  renderDemo();
}));
$("#next-step").addEventListener("click", () => { if (step < 4) { step += 1; renderDemo(); } });
$("#reset-demo").addEventListener("click", () => { step = 0; renderDemo(); });

let benchmarkData = [];
const workloadSelect = $("#workload-select");
const threadSelect = $("#thread-select");

function populateThreads() {
  const options = benchmarkData.filter((row) => row.benchmark === workloadSelect.value).map((row) => row.threads);
  threadSelect.innerHTML = options.map((value) => `<option value="${value}">${value}</option>`).join("");
  renderBenchmark();
}

function renderBenchmark() {
  const row = benchmarkData.find((item) => item.benchmark === workloadSelect.value && item.threads === Number(threadSelect.value));
  if (!row) return;
  const max = Math.max(row.baseline_median_seconds, row.monitored_median_seconds);
  $("#baseline-time").textContent = `${row.baseline_median_seconds.toFixed(6)} s`;
  $("#monitored-time").textContent = `${row.monitored_median_seconds.toFixed(6)} s`;
  $("#baseline-bar").style.width = `${row.baseline_median_seconds / max * 100}%`;
  $("#monitored-bar").style.width = `${row.monitored_median_seconds / max * 100}%`;
  $("#overhead-value").textContent = `${row.overhead_percent.toFixed(2)}%`;
  $("#difference-value").textContent = `${row.absolute_difference_seconds.toFixed(6)} s absolute difference`;
  $("#benchmark-details").innerHTML = [
    ["Iterations / thread", row.iterations.toLocaleString()],
    ["Mutex operations", row.operations.toLocaleString()],
    ["Baseline CV", `${row.baseline_cv_percent.toFixed(2)}%`],
    ["Monitored CV", `${row.monitored_cv_percent.toFixed(2)}%`]
  ].map(([label, value]) => `<div><span>${label}</span><strong>${value}</strong></div>`).join("");
  $("#data-warning").hidden = !row.baseline_below_target;
}

fetch("data/benchmark-summary.json")
  .then((response) => { if (!response.ok) throw new Error(`HTTP ${response.status}`); return response.json(); })
  .then((payload) => { benchmarkData = payload.results; populateThreads(); })
  .catch((error) => { $("#benchmark-details").innerHTML = `<div><span>Data error</span><strong>${error.message}</strong></div>`; });

workloadSelect.addEventListener("change", populateThreads);
threadSelect.addEventListener("change", renderBenchmark);
renderDemo();
