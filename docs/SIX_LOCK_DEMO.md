# Six-account network demonstration

Source: `examples/banking/network.c`. The Makefile compiles the same source with
`NETWORK_ORDERED=1` and `NETWORK_ORDERED=0` into two approved dashboard workloads.
There are six accounts, six account mutexes, six worker threads and 18 transfers.
Each worker transfers 100 cents to the next account three times; worker 6 transfers
from account 6 to account 1. Each account starts and finishes with 100000 cents.
The conserved total is 600000 cents.

## Run

In Ubuntu/WSL at the repository root, run `make monitor-build banking-build`.
Restart the backend with `make backend-run` so it loads the expanded catalog.
Keep `make frontend-run` running and refresh http://127.0.0.1:5173.

1. Start **Six-account safe network**. Six workers start together at a barrier.
   Each acquires the lower-ID account first, including worker 6 (1 then 6).
   Expected: completed, six nodes, six dependencies, zero warnings.
2. Open Lock Graph and Thread Explorer. Explain that account transfer direction
   and mutex acquisition order are different: moving money from 6 to 1 does not
   require locking 6 first. The consistent lock hierarchy prevents circular wait.
3. Start **Six-lock cycle**. Workers are intentionally joined sequentially.
   Each locks its source before its destination. The final worker adds 6 -> 1,
   closing the history 1 -> 2 -> 3 -> 4 -> 5 -> 6 -> 1.
   Expected: completed, six nodes, six dependencies, one six-lock cycle warning.
   Repeated identical dependencies do not generate duplicate warnings.
4. Open Alerts, inspect the full path, then use recorded Replay to step across
   the warning. Lock IDs are opaque monitor identities; use stdout's account and
   acquisition-order labels to explain the workload, not raw address assumptions.
5. In Reports, show all six final balances, 18 transfer lines, the total and
   cleanup status. Both examples complete normally; the risky example does not
   demonstrate simultaneous deadlock. Keep the original bounded Controlled
   deadlock scenario for that separate lesson.

Say: "A deadlock risk does not need a pair of opposite arrows. A dependency can
travel through six different locks before returning to its starting point.
The same BFS algorithm finds that longer path. Historical risk still does not
prove current deadlock: this risk example executes the workers sequentially."

Terminal alternatives: `make monitor-network-safe`, `make monitor-network-cycle`.
These targets print real JSONL monitor events on stderr.

Verification: `make check-phase5` includes a real-process integration test checking
six nodes, six edges, six thread instances, 18 transfers, conserved balances,
complete ingestion and cleanup, plus a closed seven-entry path with six distinct
locks for the risky run. This is a functional workload extension, not a new
performance benchmark or a claim to support more synchronization APIs.
