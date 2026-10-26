"""Nonblocking publication; each subscriber has an independent bounded queue."""
import asyncio
from dataclasses import dataclass, field


@dataclass(eq=False)
class Subscriber:
    queue: asyncio.Queue
    overflow: bool = False


@dataclass
class Channel:
    queue_size: int
    client_limit: int
    cursor: int = 0
    subscribers: set = field(default_factory=set)

    def subscribe(self):
        if len(self.subscribers) >= self.client_limit:
            raise ValueError("websocket_client_limit")
        subscriber = Subscriber(asyncio.Queue(maxsize=self.queue_size))
        self.subscribers.add(subscriber)
        return subscriber

    def publish(self, kind, session_id, data):
        self.cursor += 1
        envelope = {"type": kind, "session_id": session_id, "cursor": self.cursor, "data": data}
        for sub in tuple(self.subscribers):
            if sub.overflow:
                continue
            try:
                sub.queue.put_nowait(envelope)
            except asyncio.QueueFull:
                sub.overflow = True
                while not sub.queue.empty():
                    sub.queue.get_nowait()
                sub.queue.put_nowait({"type": "monitoring_error", "session_id": session_id,
                                     "cursor": self.cursor, "data": {"code": "slow_consumer",
                                     "recovery": "Reconnect for a new snapshot; inspect history gap flags."}})

    def unsubscribe(self, subscriber):
        self.subscribers.discard(subscriber)
