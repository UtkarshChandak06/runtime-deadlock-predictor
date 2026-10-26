import type { Envelope, Snapshot } from "../api/types";
export interface StreamState {
  snapshot: Snapshot | null;
  cursor: number;
  continuityLost: boolean;
}
export function applyMessage(
  state: StreamState,
  message: Envelope,
): StreamState {
  if (message.type === "session_snapshot")
    return {
      snapshot: message.data,
      cursor: message.cursor,
      continuityLost: state.continuityLost || message.data.events.gap,
    };
  if (!state.snapshot || message.cursor <= state.cursor) return state;
  const snapshot = state.snapshot;
  const continuityLost =
    state.continuityLost || message.cursor !== state.cursor + 1;
  if (message.type === "runtime_event") {
    const lastIndex = snapshot.events.items.at(-1)?.index ?? 0;
    const eventGap = message.data.index > lastIndex + 1;
    const items = [
      ...snapshot.events.items.filter((e) => e.index !== message.data.index),
      message.data,
    ]
      .sort((a, b) => a.index - b.index)
      .slice(-10000);
    return {
      cursor: message.cursor,
      continuityLost,
      snapshot: {
        ...snapshot,
        events: {
          ...snapshot.events,
          items,
          next_after: items.at(-1)!.index,
          total_received: Math.max(
            snapshot.events.total_received,
            message.data.index,
          ),
          gap: snapshot.events.gap || eventGap,
          history_truncated:
            snapshot.events.history_truncated || items[0].index > 1,
        },
        session: {
          ...snapshot.session,
          event_count: Math.max(
            snapshot.session.event_count,
            message.data.index,
          ),
        },
      },
    };
  }
  if (message.type === "session_status")
    return {
      cursor: message.cursor,
      continuityLost,
      snapshot: { ...snapshot, session: message.data },
    };
  return {
    cursor: message.cursor,
    continuityLost,
    snapshot: {
      ...snapshot,
      events: {
        ...snapshot.events,
        gap: snapshot.events.gap || message.data.code === "slow_consumer",
      },
      errors: [...snapshot.errors, message.data].slice(-20),
    },
  };
}
