# Frontend Presentation Workflow

## Objective

Create a responsive, presentation-focused frontend that explains how the
runtime deadlock predictor works and visualizes its verified benchmark results.
The frontend must not claim that it runs `LD_PRELOAD` in the browser. The first
version is a static site driven by checked-in project data; executing arbitrary
C programs remains a separate, optional Linux-backend feature.

## Scope of version 1

The site will provide:

1. A clear project introduction and one-sentence purpose.
2. An interactive safe-versus-dangerous mutex-order demonstration.
3. A lock-order graph animation showing how a cycle is detected.
4. A simulated terminal displaying the real warning format.
5. An architecture section for hooks, tracker, graph, report, and logger.
6. Benchmark charts generated only from the audited summary CSV.
7. Verification status, supported behavior, and honest limitations.
8. A presentation mode suitable for a classroom or project review.

It will not initially provide:

- Browser compilation or execution of C programs.
- Upload and execution of arbitrary binaries.
- Live Linux process monitoring.
- Authentication, a database, or editable benchmark results.
- Claims beyond the verified Phase 6/7 evidence.

## Proposed source layout

```text
frontend/
|-- index.html
|-- styles.css
|-- app.js
|-- data/
|   |-- benchmark-summary.json
|   `-- architecture.json
|-- assets/
|   `-- icons/
`-- README.md
```

The first version should use buildless HTML, CSS, and JavaScript. This keeps the
demo portable: it can be opened through a small local HTTP server, hosted on a
static provider, or published through GitHub Pages without a Node build step.

## Page structure

### 1. Hero and project summary

- Title: Runtime Deadlock Predictor.
- Short explanation: monitors pthread mutex order and warns about cycles.
- Primary action: `Start demonstration`.
- Secondary action: `View benchmark evidence`.
- Status badges: Linux/WSL, C, pthreads, `LD_PRELOAD`, Phase 6/7 verified.

### 2. Problem demonstration

Present two threads and two locks:

```text
Thread 1: lock A -> lock B
Thread 2: lock B -> lock A
```

Controls:

- `Next step` advances one acquisition attempt.
- `Reset` restores the initial graph.
- `Safe order` runs a consistent A-then-B sequence.
- `Dangerous order` creates A -> B -> A.

The graph must distinguish:

- Held mutex: blue.
- Requested mutex: amber.
- Normal dependency edge: neutral.
- Cycle-closing edge and cycle path: red.

The explanation must say “potential deadlock,” not “confirmed deadlock.”

### 3. Monitor pipeline

Show the actual flow:

```text
Application
    -> LD_PRELOAD hooks
    -> per-thread held-lock tracker
    -> lock-order graph and BFS cycle search
    -> warning formatter
    -> bounded logger
    -> original pthread operation
```

Each component should have a short description linked to the corresponding
source file in `src/`.

### 4. Warning output

Use the real record structure:

```text
[WARNING] Potential Deadlock Detected (not a confirmed deadlock)
Thread: tid=<Linux thread ID>
Held Lock: <address H>
Requested Lock: <address L>
Cycle: <L> -> ... -> <H> -> <L>
```

This is a deterministic presentation simulation. It must be labelled as an
example, not output from a live browser execution.

### 5. Benchmark dashboard

Source data:

```text
benchmarks/results/20261015T224443+0530/summary.csv
benchmarks/results/20261015T224443+0530/environment.json
```

During implementation, convert the audited summary rows into
`frontend/data/benchmark-summary.json`. Do not manually invent or round values
before conversion. Display:

- Baseline versus monitored median time.
- Absolute difference.
- Percentage overhead.
- Workload and thread-count selector.
- Variability or warning indicator for short/noisy cases.
- Host-specific measurement disclaimer.

The old result set may appear only as historical evidence and must not replace
the corrected audited results.

### 6. Verification and limitations

Show the successfully exercised categories:

- Bare and monitored regression tests.
- Diagnostic validation.
- ThreadSanitizer.
- AddressSanitizer and UndefinedBehaviorSanitizer.
- Resolver and robustness tests.
- Baseline-versus-monitored benchmark validation.

Show unsupported or partial behavior separately, including trylocks, timed
locks, condition waits, robust owner-death tracking, static executables,
cross-process synchronization, and lifecycle/address-reuse limitations.

## Visual direction

- Dark technical interface with high-contrast text.
- Restrained blue, amber, and red state colors.
- Monospace typography for locks, addresses, warnings, and commands.
- Regular sans-serif typography for explanations.
- Minimal decorative imagery; the graph and benchmark evidence are the primary
  visuals.
- Responsive layout for laptop presentation and mobile viewing.
- Respect `prefers-reduced-motion`; animations must also work step-by-step.

## Implementation stages

### Stage 1: Data preparation

1. Read the corrected summary and environment files.
2. Generate a JSON representation without changing numeric values.
3. Validate that it contains 13 workload/thread groups and five samples per
   mode in the corresponding raw CSV.
4. Record the source result directory in the JSON metadata.

Exit condition: JSON values reproduce every summary median and overhead.

### Stage 2: Static page foundation

1. Create semantic HTML sections and navigation.
2. Add the responsive visual system and accessible focus states.
3. Add source links and evidence labels.
4. Ensure the page remains understandable without JavaScript.

Exit condition: all content is readable at mobile and desktop widths.

### Stage 3: Interactive lock graph

1. Implement the safe sequence state machine.
2. Implement the dangerous sequence state machine.
3. Draw graph nodes and directed edges using inline SVG.
4. Highlight the complete cycle and explain the closing edge.
5. Add reset, keyboard operation, and reduced-motion behavior.

Exit condition: repeated safe runs never show a warning; the dangerous sequence
shows exactly one potential-deadlock demonstration with a complete cycle.

### Stage 4: Benchmark visualization

1. Load the checked-in JSON.
2. Provide workload and thread filters.
3. Render baseline and monitored comparisons with an accessible table fallback.
4. Show absolute difference, percentage overhead, CV, and warning flags.
5. Add the host-specific and synthetic-workload disclaimers.

Exit condition: displayed numbers match the audited CSV exactly within display
rounding, and the unrounded values remain available in accessible labels.

### Stage 5: Quality assurance

Validate:

- No course, faculty, student name, or registration metadata is introduced.
- No fabricated runtime output or benchmark result appears.
- All links and controls work with keyboard navigation.
- Color is not the only indication of graph state.
- Layout works at approximately 360 px, 768 px, and 1440 px widths.
- The browser console has no errors.
- The site functions through an HTTP server rather than relying on permissive
  `file://` behavior.

Recommended local preview from the project root:

```bash
python3 -m http.server 8080 --directory frontend
```

Then open `http://localhost:8080`.

### Stage 6: Repository integration

1. Review `git diff` and confirm only frontend-related files changed.
2. Run the frontend checks and retain the existing C verification evidence.
3. Commit the frontend separately from monitor changes.
4. Push to `main` only after visual and data verification.

Suggested commit message:

```text
Add interactive project presentation frontend
```

### Stage 7: Optional deployment

Deploy the static `frontend/` directory privately first. Verify the deployed URL
and responsive behavior before changing visibility. GitHub Pages or another
static host can be used later if public access is required.

## Optional version 2: live execution backend

A later version could add a local Linux service that accepts predefined safe
examples, compiles them in an isolated environment, runs them with the monitor,
and streams structured results to the frontend. Arbitrary user code must not be
enabled without strong sandboxing, resource limits, timeouts, and a separate
security review.

Version 2 is outside the presentation frontend scope and must not begin until
the static version is complete and verified.

## Definition of done

The frontend is complete when:

- A new viewer can explain the project after following the demonstration.
- The graph correctly distinguishes safe order from a cycle.
- All displayed measurements trace to the corrected audited artifacts.
- Limitations are visible rather than hidden.
- The page is responsive and keyboard accessible.
- Source, tests, and production monitor architecture remain unchanged.
- The frontend can be served locally with one documented command.
