import { useEffect, useState } from "react";
import { Empty, Notice, Panel } from "../components/Common";
export interface BenchmarkRow {
  workload: string;
  threads: number;
  mode: string;
  repetitions: number;
  iterations_per_thread: number;
  baseline_median_ns: number;
  monitored_median_ns: number;
  absolute_overhead_ns: number;
  overhead_percent: number;
}
export interface BenchmarkEvidence {
  kind: string;
  source: string;
  environment: {
    date_utc: string;
    cpu: string;
    raw_rows: number;
    limitations: string[];
  };
  summary: BenchmarkRow[];
  method: string;
  raw_csv: string;
  summary_csv: string;
}
export function Performance() {
  const [data, setData] = useState<BenchmarkEvidence | null>(null),
    [error, setError] = useState("");
  useEffect(() => {
    const controller = new AbortController();
    fetch("/benchmark-results.json", { signal: controller.signal })
      .then(async (response) => {
        if (!response.ok)
          throw new Error(
            "No saved benchmark set is available. Run make benchmark, then export a completed results directory.",
          );
        const result = (await response.json()) as BenchmarkEvidence;
        if (
          result.kind !== "saved-measurements" ||
          !Array.isArray(result.summary)
        )
          throw new Error("Invalid saved benchmark format.");
        setData(result);
      })
      .catch((error) => {
        if (error.name !== "AbortError") setError(error.message);
      });
    return () => controller.abort();
  }, []);
  if (error) return <Notice>{error}</Notice>;
  if (!data) return <Empty title="Loading saved measurements" />;
  return (
    <Panel
      title="Measured runtime overhead"
      subtitle="Saved local C benchmark results — not a live session metric"
      action={<span className="tag">{data.environment.raw_rows} raw runs</span>}
    >
      <p className="scope-note">
        {data.method} Collected {data.environment.date_utc} on{" "}
        {data.environment.cpu}.
      </p>
      <Notice>
        These small mutex workloads expose instrumentation cost. Percentages
        depend strongly on short baseline times and WSL scheduling; they are not
        a prediction accuracy or a general application slowdown estimate.
      </Notice>
      <div className="table-scroll">
        <table>
          <thead>
            <tr>
              <th>Workload</th>
              <th>Threads</th>
              <th>Sink</th>
              <th>Baseline median</th>
              <th>Monitored median</th>
              <th>Absolute overhead</th>
              <th>Overhead</th>
            </tr>
          </thead>
          <tbody>
            {data.summary.map((row) => (
              <tr key={row.workload + row.threads + row.mode}>
                <td>{row.workload}</td>
                <td>{row.threads}</td>
                <td>
                  {row.mode === "monitored_file"
                    ? "Full JSONL → file"
                    : "Full JSONL → /dev/null"}
                </td>
                <td>{(row.baseline_median_ns / 1e6).toFixed(3)} ms</td>
                <td>{(row.monitored_median_ns / 1e6).toFixed(3)} ms</td>
                <td>{(row.absolute_overhead_ns / 1e6).toFixed(3)} ms</td>
                <td>{row.overhead_percent.toFixed(1)}%</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
      <div className="selection">
        <h3>Method and provenance</h3>
        <p className="mono">{data.source}</p>
        <p>
          File output is validated after timing. /dev/null discards records but
          still executes analysis, formatting and writes; it is not an
          analysis-only measurement.
        </p>
        <ul>
          {data.environment.limitations.map((item) => (
            <li key={item}>{item}</li>
          ))}
        </ul>
        <div className="actions">
          <a href={"/" + data.raw_csv} download>
            Download raw CSV
          </a>
          <a href={"/" + data.summary_csv} download>
            Download summary CSV
          </a>
        </div>
      </div>
    </Panel>
  );
}
