# Presentation frontend

This static site explains the runtime deadlock predictor and visualizes the
corrected, audited Phase 6/7 benchmark data. It does not execute `LD_PRELOAD` or
arbitrary C code in the browser.

From the project root, preview it with:

```bash
python3 -m http.server 8080 --directory frontend
```

Then open <http://localhost:8080>.

The benchmark view is sourced from
`benchmarks/results/20261015T224443+0530/summary.csv`. If that audited dataset
changes, update `frontend/data/benchmark-summary.json` and re-run the validation
described in `docs/FRONTEND_WORKFLOW.md`.
