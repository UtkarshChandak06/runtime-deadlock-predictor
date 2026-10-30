"""Single-event-loop session ownership and bounded evidence retention."""
import asyncio
from collections import deque
from copy import deepcopy
from datetime import datetime, timezone
import uuid
import time
import json
from fastapi import HTTPException
from .event_parser import JSONLParser, ParseIssue
from .graph_projection import Projection
from .models import SessionView, TERMINAL
from .process_runner import CATALOG, dependencies, supervise, group_absent
from .websocket_manager import Channel


def now():
    return datetime.now(timezone.utc)


class Session:
    def __init__(self, application, settings):
        self.application = application
        self.settings = settings
        self.session_id = str(uuid.uuid4())
        self.state = 'created'
        self.created_at = now()
        self.started_at = self.ended_at = self.pid = self.exit_code = None
        self.event_count = self.invalid_records = self.dropped_events = 0
        self.c_monitor_complete = None
        self.ingestion_complete = True
        self.cleanup_verified = False
        self.error = self.controlled_deadlock_evidence = None
        self.outputs = {'stdout': bytearray(), 'stderr': bytearray()}
        self.truncated = {'stdout': False, 'stderr': False}
        self.events = deque(maxlen=settings.max_events)
        self.errors = deque(maxlen=20)
        self.parser = JSONLParser(settings.max_line_bytes)
        self.projection = Projection(settings.projection_limit)
        self.channel = Channel(settings.ws_queue, settings.max_ws_clients)
        self.stop_event = asyncio.Event()
        self.task = None
        self.last_sequence = self.last_timestamp = 0
        self.saw_start = self.saw_end = False
        self.metadata = getattr(application, 'metadata', None)
        self.runner = supervise
        # Bubblewrap init is PID 1, trusted wait parent PID 2, user target PID 3.
        # Do not run the target as init: PID 1 ignores ordinary fatal signals.
        self.expected_event_pid = 3 if self.metadata else None
        self.mutex_activity = 0
        self.persist = None
        self.last_saved = self.last_output_status = 0

    @property
    def monitoring_complete(self):
        return self.c_monitor_complete and self.ingestion_complete if self.c_monitor_complete is not None else None

    def view(self):
        return SessionView(session_id=self.session_id, application_id=self.application.application_id,
            target_kind='user_build' if self.metadata else 'example', build_metadata=self.metadata,
            mutex_activity=self.mutex_activity,
            observation=('potential_lock_order_cycle' if self.projection.warning_count else
                         'no_relevant_mutex_activity' if self.state in TERMINAL and not self.mutex_activity else
                         'no_cycle_observed' if self.mutex_activity else 'not_observed'),
            state=self.state, created_at=self.created_at, started_at=self.started_at, ended_at=self.ended_at,
            pid=self.pid, exit_code=self.exit_code, event_count=self.event_count,
            warning_count=self.projection.warning_count, invalid_records=self.invalid_records,
            c_monitor_complete=self.c_monitor_complete, ingestion_complete=self.ingestion_complete,
            monitoring_complete=self.monitoring_complete, history_truncated=bool(self.dropped_events),
            dropped_events=self.dropped_events, stdout=self.outputs['stdout'].decode('utf-8','replace'),
            stderr=self.outputs['stderr'].decode('utf-8','replace'), stdout_truncated=self.truncated['stdout'],
            stderr_truncated=self.truncated['stderr'], error=self.error, cursor=self.channel.cursor,
            cleanup_verified=self.cleanup_verified, controlled_deadlock_evidence=self.controlled_deadlock_evidence).model_dump(mode='json')

    def publish(self, kind, data):
        self.channel.publish(kind, self.session_id, deepcopy(data))

    def status(self):
        self.publish('session_status', self.view())
        if self.persist:self.persist(self)

    def issue(self, code, invalid=False):
        self.ingestion_complete = False
        self.invalid_records += int(invalid)
        self.errors.append({'code': code})
        self.publish('monitoring_error', {'code': code})

    def ingest(self, record):
        if isinstance(record, ParseIssue):
            self.issue(record.reason, True); return
        if record['process_id'] != (self.expected_event_pid or self.pid) or record['sequence'] <= self.last_sequence:
            self.issue('pid_or_sequence_mismatch', True); return
        if record['timestamp_ns'] < self.last_timestamp:
            self.issue('timestamp_regression', True); return
        if record['sequence'] != self.last_sequence + 1:
            self.issue('sequence_gap')
        self.last_sequence = record['sequence']
        self.last_timestamp = record['timestamp_ns']
        self.c_monitor_complete = record['monitoring_complete'] and self.c_monitor_complete is not False
        self.saw_start |= record['session_event'] == 'session_start'
        self.saw_end |= record['session_event'] == 'session_end'
        self.event_count += 1
        self.mutex_activity += record['session_event'] == 'lock_request'
        if len(self.events) == self.events.maxlen:
            self.dropped_events += 1
        item = {'index': self.event_count, 'event': record}
        self.events.append(item)
        self.projection.consume(record, bool(self.monitoring_complete))
        self.publish('runtime_event', item)
        if self.persist and time.monotonic()-self.last_saved>.25:
            self.persist(self);self.last_saved=time.monotonic()

    def feed(self, data):
        for record in self.parser.feed(data):
            self.ingest(record)

    def output(self, name, data):
        remaining = self.settings.max_output_bytes - len(self.outputs[name])
        self.outputs[name].extend(data[:remaining])
        self.truncated[name] |= len(data) > remaining
        if time.monotonic()-self.last_output_status>.25:
            self.status();self.last_output_status=time.monotonic()

    def page(self, after=0, limit=200):
        oldest = self.events[0]['index'] if self.events else 1
        items = [item for item in self.events if item['index'] > after][:limit]
        return {'items': items, 'next_after': items[-1]['index'] if items else after,
                'oldest_available': oldest, 'total_received': self.event_count,
                'gap': after < oldest - 1, 'history_truncated': bool(self.dropped_events)}

    def alerts(self):
        return {'alerts': list(self.projection.alerts), 'total_received': self.projection.warning_count,
                'truncated': self.projection.warning_count > len(self.projection.alerts)}

    def snapshot(self):
        return deepcopy({'type': 'session_snapshot', 'session_id': self.session_id, 'cursor': self.channel.cursor,
            'data': {'session': self.view(), 'events': self.page(limit=self.settings.max_events),
                     'graph': self.projection.graph(self.monitoring_complete),
                     'threads': self.projection.thread_view(bool(self.monitoring_complete)),
                     'alerts': self.alerts(), 'errors': list(self.errors)}})

    async def run(self):
        def started(pid):
            self.pid = pid; self.started_at = now(); self.state = 'running'; self.status()
        try:
            result = await self.runner(self.settings, self.application, self.stop_event, started, self.feed, self.output)
            for record in self.parser.finish():
                self.ingest(record)
            actual=getattr(result,'target_returncode',None)
            self.exit_code = actual if self.metadata else result.exit_code
            if self.metadata:self.metadata['supervisor_exit_code']=result.exit_code
            self.cleanup_verified = result.cleanup_verified
            if not self.saw_start or not self.saw_end or not result.drain_complete:
                self.issue('incomplete_stream_lifecycle')
            self.state = 'stopped' if result.stopped else 'timed_out' if result.timed_out else 'completed' if self.exit_code == 0 and (self.event_count or self.metadata) else 'failed'
            if getattr(result,'limit_hit',None):
                self.state='failed';self.error=result.limit_hit;self.issue(result.limit_hit)
            if self.state == 'failed':
                self.error = self.error or ('no_monitor_events' if not self.event_count and not self.metadata else 'nonzero_exit')
                if actual is not None and actual<0:self.error='crash_signal_'+str(-actual)
            if self.application.deadlock and result.timed_out:
                waiting = [t for t in self.projection.threads.values() if t['last_requested_lock'] and t['last_observed_held_locks']]
                conflict = len(waiting) == 2 and all(t['last_requested_lock'] in waiting[1-i]['last_observed_held_locks'] for i,t in enumerate(waiting))
                futex = len(result.wait_channels) == 3 and all('futex' in w for w in result.wait_channels)
                stdout = self.outputs['stdout'].decode('utf-8', 'replace')
                markers = all(marker in stdout for marker in ('HELD worker=1 account=1', 'HELD worker=2 account=2',
                    'REQUEST worker=1 held=1 wanted=2', 'REQUEST worker=2 held=2 wanted=1'))
                self.controlled_deadlock_evidence = {'expected_timeout': True, 'conflicting_requests_observed': conflict,
                    'three_futex_waiters_observed': futex, 'c_warning_observed': bool(self.projection.warning_count),
                    'stdout_markers_observed': markers,
                    'fixture_verified': conflict and futex and markers and bool(self.projection.warning_count),
                    'scope': 'controlled banking fixture only; historical cycles do not confirm deadlock'}
        except asyncio.CancelledError:
            # supervise's finally has already killed/reaped the child. Keep the
            # externally visible lifecycle terminal even when its task is cancelled.
            self.state = 'stopped'; self.error = 'supervisor_cancelled'
            self.issue('supervisor_cancelled')
            self.cleanup_verified = self.pid is None or group_absent(self.pid)
            raise
        except Exception as error:
            self.state = 'failed'; self.error = 'supervisor_failure:' + type(error).__name__
            self.issue('supervisor_failure')
            self.cleanup_verified = self.pid is None or group_absent(self.pid)
        finally:
            self.ended_at = now(); self.status()


class SessionManager:
    def __init__(self, settings):
        self.settings = settings
        self.sessions = {}
        self.closing = False
        self.workspace = None
        if settings.workspace_path is not None:
            from .workspace import Workspace
            self.workspace = Workspace(settings,self)
            self.restore()

    def save(self,session):
        if self.workspace:
            # Bound each retained snapshot as well as the total SQLite file.
            # Drop oldest retained events explicitly; keep total observed counts.
            snapshot=session.snapshot()['data']
            while len(json.dumps(snapshot))>2097152 and session.events:
                session.events.popleft();session.dropped_events+=1
                snapshot=session.snapshot()['data']
            if len(json.dumps(snapshot))>2097152:
                session.projection.complete=False
                for t in session.projection.threads.values():t['history'].clear()
                session.projection.alerts.clear()
                session.errors.append({'code':'persistence_projection_retention_limit'})
                snapshot=session.snapshot()['data']
            try:self.workspace.store.put('run',session.session_id,snapshot)
            except HTTPException:
                # Storage failure must never bypass process cleanup or leave an
                # active job task crashing while trying to persist its error.
                session.persist=None
                session.error=session.error or 'persistence_storage_unavailable'
                session.errors.append({'code':'persistence_storage_unavailable_export_before_restart'})

    def restore(self):
        from .workspace import UserTarget
        for snapshot in self.workspace.store.all('run'):
            v=snapshot['session'];meta=v.get('build_metadata')
            app=UserTarget(v['application_id'],meta) if meta else CATALOG.get(v['application_id'])
            if app is None:continue
            s=Session(app,self.settings)
            for name in ('session_id','state','created_at','started_at','ended_at','pid','exit_code',
                         'event_count','invalid_records','dropped_events','c_monitor_complete',
                         'ingestion_complete','cleanup_verified','error','controlled_deadlock_evidence','mutex_activity'):
                if name in v:setattr(s,name,v[name])
            s.outputs={name:bytearray(v[name].encode()) for name in ('stdout','stderr')}
            s.truncated={name:v[name+'_truncated'] for name in ('stdout','stderr')}
            s.events=deque(snapshot['events']['items'],maxlen=self.settings.max_events)
            s.errors=deque(snapshot.get('errors',[]),maxlen=20)
            s.projection.nodes={n['lock_id']:n for n in snapshot['graph']['nodes']}
            s.projection.edges={(e['from_lock'],e['to_lock']):e for e in snapshot['graph']['edges']}
            s.projection.alerts=deque(snapshot['alerts']['alerts'],maxlen=self.settings.projection_limit)
            s.projection.warning_count=snapshot['alerts']['total_received']
            s.projection.complete=snapshot['graph']['projection_complete']
            for t in snapshot['threads']['threads']:
                t['history']=deque(t['history'],maxlen=20)
                s.projection.threads[(t['thread_id'],t['thread_instance'])]=t
            s.channel.cursor=v['cursor']
            if s.state not in TERMINAL:
                s.state='stopped';s.error='backend_restarted';s.ingestion_complete=False
                s.cleanup_verified=False;s.ended_at=now()
            s.persist=self.save;self.sessions[s.session_id]=s;self.save(s)

    def delete(self,identifier):
        s=self.get(identifier)
        if s.state not in TERMINAL:raise HTTPException(409,'stop_run_before_deletion')
        if self.workspace:self.workspace.store.delete('run',identifier)
        del self.sessions[identifier]

    def create_user(self,application,runner):
        if self.closing:raise HTTPException(503,'backend_shutting_down')
        if self.active()>=self.settings.max_concurrent:raise HTTPException(429,'concurrent_session_limit')
        if len(self.sessions)>=min(20,self.settings.max_sessions):raise HTTPException(429,'retained_session_limit_delete_a_run')
        s=Session(application,self.settings);s.runner=runner;s.persist=self.save
        self.sessions[s.session_id]=s
        s.state='starting';s.status();s.task=asyncio.create_task(s.run())
        return s

    def active(self):
        return sum(s.state in {'starting', 'running'} for s in self.sessions.values())

    def catalog(self):
        result = []
        for app in CATALOG.values():
            reason = None
            try:
                dependencies(self.settings.root, app)
            except (OSError, ValueError) as error:
                reason = str(error)
            result.append(dict(application_id=app.application_id, display_name=app.display_name,
                description=app.description, scenario=app.scenario, safe_to_run_normally=not app.deadlock,
                bounded_supervision=app.deadlock, executable=app.executable, available=reason is None, unavailable_reason=reason))
        return result

    def get(self, identifier):
        if identifier not in self.sessions:
            raise HTTPException(404, 'unknown_session')
        return self.sessions[identifier]

    def validate_start(self, app):
        if self.closing:
            raise HTTPException(503, 'backend_shutting_down')
        if self.active() >= self.settings.max_concurrent:
            raise HTTPException(429, 'concurrent_session_limit')
        try:
            dependencies(self.settings.root, app)
        except (OSError, ValueError):
            raise HTTPException(503, 'approved_build_dependencies_unavailable')

    def create(self, request):
        app = CATALOG.get(request.application_id)
        if app is None:
            raise HTTPException(404, 'unknown_application')
        if len(self.sessions) >= (min(20,self.settings.max_sessions) if self.workspace else self.settings.max_sessions):
            raise HTTPException(429, 'retained_session_limit_restart_to_clear')
        self.validate_start(app)
        session = Session(app, self.settings)
        session.persist = self.save
        self.sessions[session.session_id] = session
        if request.auto_start:
            self.start(session)
        else:self.save(session)
        return session

    def start(self, session):
        if session.state != 'created':
            raise HTTPException(409, 'session_not_created')
        self.validate_start(session.application)
        session.state = 'starting'; session.status()
        session.task = asyncio.create_task(session.run())

    async def stop(self, session):
        if session.state in TERMINAL:
            raise HTTPException(409, 'session_already_terminal')
        if session.state == 'created':
            session.state = 'stopped'; session.ended_at = now(); session.cleanup_verified = True; session.status()
        else:
            session.stop_event.set()
            await asyncio.shield(session.task)

    async def close(self):
        self.closing = True
        if self.workspace:await self.workspace.close()
        await asyncio.gather(*(self.stop(s) for s in self.sessions.values() if s.state not in TERMINAL))
        if self.workspace:self.workspace.store.close()
