import asyncio
from contextlib import asynccontextmanager
import ipaddress
from urllib.parse import urlsplit
from fastapi import FastAPI, Query, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from .config import Settings
from .models import (SessionCreate, SessionView, ApplicationView, HealthView, EventPage,
                     ProjectionView, ThreadsView, AlertsView, TERMINAL)
from .session_manager import SessionManager


class LocalBoundary:
    def __init__(self, app, origins):
        self.app, self.origins = app, origins

    async def __call__(self, scope, receive, send):
        if scope['type'] not in {'http', 'websocket'}:
            return await self.app(scope, receive, send)
        headers = dict(scope['headers'])
        try:
            local = ipaddress.ip_address(scope['client'][0]).is_loopback
            host = urlsplit('http://' + headers.get(b'host', b'').decode()).hostname
            valid = local and host in {'localhost', '127.0.0.1', '::1'}
            origin = headers.get(b'origin')
            valid &= origin is None or origin.decode() in self.origins
        except (ValueError, UnicodeError, TypeError):
            valid = False
        if not valid:
            if scope['type'] == 'websocket':
                return await send({'type': 'websocket.close', 'code': 1008})
            await send({'type': 'http.response.start', 'status': 403, 'headers': [(b'content-type', b'application/json')]})
            return await send({'type': 'http.response.body', 'body': b'{"detail":"local_origin_required"}'})
        if scope['type'] == 'http' and scope['method'] == 'POST':
            body = bytearray()
            try:
                async with asyncio.timeout(3):
                    while True:
                        message = await receive()
                        if message['type'] == 'http.disconnect':
                            return
                        body.extend(message.get('body', b''))
                        if len(body) > 1024:
                            raise ValueError('body_limit')
                        if not message.get('more_body'):
                            break
            except (TimeoutError, ValueError):
                await send({'type': 'http.response.start', 'status': 413, 'headers': []})
                return await send({'type': 'http.response.body', 'body': b'body_limit_or_timeout'})
            delivered = False
            async def buffered_receive():
                nonlocal delivered
                if not delivered:
                    delivered = True
                    return {'type': 'http.request', 'body': bytes(body), 'more_body': False}
                return await receive()
            return await self.app(scope, buffered_receive, send)
        await self.app(scope, receive, send)


def create_app(settings=None):
    settings = settings or Settings.from_env()
    manager = SessionManager(settings)
    @asynccontextmanager
    async def lifespan(app):
        yield
        await manager.close()
    app = FastAPI(title='Deadlock Sentinel', version='5.0', lifespan=lifespan)
    app.state.manager = manager
    app.add_middleware(CORSMiddleware, allow_origins=list(settings.origins), allow_methods=['GET','POST'], allow_headers=['Content-Type'])
    app.add_middleware(LocalBoundary, origins=settings.origins)

    @app.get('/api/v1/health', response_model=HealthView)
    async def health():
        return dict(active_sessions=manager.active(), retained_sessions=len(manager.sessions))

    @app.get('/api/v1/applications', response_model=list[ApplicationView])
    async def applications():
        return manager.catalog()

    @app.post('/api/v1/sessions', response_model=SessionView, status_code=201)
    async def create(request: SessionCreate):
        return manager.create(request).view()

    @app.get('/api/v1/sessions', response_model=list[SessionView])
    async def sessions():
        return [s.view() for s in manager.sessions.values()]

    @app.get('/api/v1/sessions/{identifier}', response_model=SessionView)
    async def session(identifier: str):
        return manager.get(identifier).view()

    @app.post('/api/v1/sessions/{identifier}/start', response_model=SessionView)
    async def start(identifier: str):
        s = manager.get(identifier); manager.start(s); return s.view()

    @app.post('/api/v1/sessions/{identifier}/stop', response_model=SessionView)
    async def stop(identifier: str):
        s = manager.get(identifier); await manager.stop(s); return s.view()

    @app.get('/api/v1/sessions/{identifier}/events', response_model=EventPage)
    async def events(identifier: str, after: int = Query(0, ge=0), limit: int = Query(200, ge=1, le=1000)):
        return manager.get(identifier).page(after, limit)

    @app.get('/api/v1/sessions/{identifier}/graph', response_model=ProjectionView)
    async def graph(identifier: str):
        s = manager.get(identifier); return s.projection.graph(s.monitoring_complete)

    @app.get('/api/v1/sessions/{identifier}/threads', response_model=ThreadsView)
    async def threads(identifier: str):
        s = manager.get(identifier); return s.projection.thread_view(bool(s.monitoring_complete))

    @app.get('/api/v1/sessions/{identifier}/alerts', response_model=AlertsView)
    async def alerts(identifier: str):
        return manager.get(identifier).alerts()

    @app.websocket('/ws/v1/sessions/{identifier}')
    async def stream(ws: WebSocket, identifier: str):
        s = manager.sessions.get(identifier)
        if s is None:
            await ws.close(code=1008); return
        try:
            subscriber = s.channel.subscribe()
        except ValueError:
            await ws.close(code=1013); return
        # No await between subscription and snapshot: all mutations share this event loop.
        snapshot = s.snapshot()
        tasks = []
        try:
            await ws.accept()
            await asyncio.wait_for(ws.send_json(snapshot), 2)
            if snapshot['data']['session']['state'] in TERMINAL:
                await ws.close(); return
            async def sender():
                while True:
                    message = await subscriber.queue.get()
                    await asyncio.wait_for(ws.send_json(message), 2)
                    if subscriber.overflow:
                        await ws.close(code=1013); return
                    if message['type'] == 'session_status' and message['data']['state'] in TERMINAL:
                        await ws.close(); return
            async def receiver():
                await ws.receive()
                # This is an output stream, not a command channel.
            tasks = [asyncio.create_task(sender()), asyncio.create_task(receiver())]
            done, _ = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
            for task in done:
                task.result()
        except (WebSocketDisconnect, TimeoutError, RuntimeError, OSError):
            pass
        finally:
            for task in tasks:
                task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)
            s.channel.unsubscribe(subscriber)
            try:
                await asyncio.wait_for(ws.close(), 1)
            except (WebSocketDisconnect, RuntimeError, OSError, TimeoutError):
                pass
    return app


app = create_app()
