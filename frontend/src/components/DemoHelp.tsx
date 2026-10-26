export function DemoHelp() {
  return (
    <details className="demo-help">
      <summary>Explain this demo · beginner guide</summary>
      <div className="help-grid">
        <article>
          <h3>Threads share resources</h3>
          <p>
            A thread is one worker in a process. In the banking example, two
            workers update shared account balances.
          </p>
        </article>
        <article>
          <h3>Mutexes protect a critical section</h3>
          <p>
            A mutex lets one owner use protected data at a time. Each banking
            account has its own lock.
          </p>
        </article>
        <article>
          <h3>Read an arrow: A → B</h3>
          <p>
            The monitor observed a request for B while A was held. It is a
            historical dependency, not a live waiting arrow.
          </p>
        </article>
        <article>
          <h3>A cycle is a risk pattern</h3>
          <p>
            Opposite orders can allow circular waiting. The unsafe demonstration
            completes sequentially; its warning does not prove current deadlock.
          </p>
        </article>
        <article>
          <h3>Compare the three programs</h3>
          <p>
            Safe uses consistent ordering. Unsafe shows conflicting historical
            orders. Controlled deadlock intentionally blocks and is terminated
            by the backend deadline.
          </p>
        </article>
        <article>
          <h3>Slow down the explanation, not the program</h3>
          <p>
            Recorded Session Replay steps through genuine retained events. Its
            playback speed does not change original timestamps or run a new
            process.
          </p>
        </article>
      </div>
      <p className="footnote">
        Educational context only. Actual runtime evidence is shown separately
        below.
      </p>
    </details>
  );
}
