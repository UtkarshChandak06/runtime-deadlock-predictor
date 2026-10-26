"""Unit fixtures are explicit; integration tests launch real GCC banking binaries."""
import asyncio
import json
import os
from pathlib import Path
import socket
import tempfile
import unittest
from urllib.request import Request, urlopen
from urllib.error import HTTPError
import uvicorn
from websockets.asyncio.client import connect
from websockets.exceptions import ConnectionClosed, InvalidStatus
from app.config import Settings
from app.main import create_app
from app.event_parser import JSONLParser, ParseIssue, parse_record
from app.graph_projection import Projection
from app.models import SessionCreate
from app.session_manager import SessionManager, Session
from app.process_runner import CATALOG, group_absent
from app.websocket_manager import Channel


def fixture(**values):
    return dict(schema_version=1, session_event='session_start', timestamp_ns=1,
                process_id=123, thread_id=123, thread_instance=0, sequence=1,
                monitoring_complete=True, **values)


class Units(unittest.TestCase):
    def test_parser_partial_multiple_and_empty(self):
        parser = JSONLParser()
        data = json.dumps(fixture()).encode() + b'\n'
        self.assertEqual(parser.feed(data[:13]), [])
        self.assertEqual(parser.feed(data[13:] + data), [fixture(), fixture()])
        self.assertEqual(parser.finish(), [])
        parser.feed(b'{')
        self.assertIsInstance(parser.finish()[0], ParseIssue)

    def test_parser_rejections_and_bounds(self):
        for data in [b'{}', b'[]', b'garbage', b'\xff', b'{"a":1,"a":2}',
                     json.dumps({**fixture(), 'schema_version': True}).encode(),
                     json.dumps({**fixture(), 'session_event': 'invented'}).encode(),
                     json.dumps({**fixture(), 'sequence': -1}).encode(),
                     json.dumps({**fixture(), 'session_event': 'lock_request'}).encode()]:
            self.assertIsInstance(parse_record(data), ParseIssue)
        parser = JSONLParser(256)
        self.assertEqual(len(parser.feed(b'x'*10000)), 1)
        self.assertLessEqual(len(parser.buffer), 256)
        self.assertEqual(parser.feed(b'x\n'), [])

    def test_projection_is_not_cycle_detector(self):
        p = Projection()
        for source, target in [('lock_1','lock_2'), ('lock_2','lock_1')]:
            event = {**fixture(), 'session_event':'lock_request', 'thread_instance':1,
                     'lock_id':target, 'held_locks':[source], 'held_depths':[1], 'lock_kind':'normal'}
            p.consume(event, True)
        self.assertEqual(len(p.edges), 2)
        self.assertEqual(list(p.alerts), [])
        p.consume({**event, 'lock_kind':'recursive', 'held_locks':['lock_1','lock_2'], 'held_depths':[1,1]}, True)
        self.assertEqual(len(p.edges), 2)

    def test_slow_queue_is_bounded(self):
        channel = Channel(1,1); sub = channel.subscribe()
        channel.publish('runtime_event','unit',{})
        channel.publish('runtime_event','unit',{})
        self.assertTrue(sub.overflow)
        self.assertEqual(sub.queue.qsize(),1)
        self.assertEqual(sub.queue.get_nowait()['data']['code'],'slow_consumer')
        with self.assertRaises(ValueError): channel.subscribe()

    def test_ingestion_invalid_identity_and_output_limits(self):
        s = Session(CATALOG['banking_safe'], Settings(max_output_bytes=64))
        s.pid = 123
        s.ingest(fixture()); s.ingest(fixture()); s.feed(b'bad\n')
        self.assertEqual(s.invalid_records,2)
        self.assertFalse(s.ingestion_complete)
        s.output('stdout',b'a'*200)
        self.assertEqual(len(s.outputs['stdout']),64)
        self.assertTrue(s.truncated['stdout'])


class Integration(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.app = create_app(Settings(max_sessions=20))
        self.manager = self.app.state.manager
        self.sock = socket.socket()
        self.sock.bind(('127.0.0.1',0))
        self.port = self.sock.getsockname()[1]
        self.server = uvicorn.Server(uvicorn.Config(self.app, log_level='error', proxy_headers=False,
                                                   ws='websockets-sansio', lifespan='on'))
        self.task = asyncio.create_task(self.server.serve(sockets=[self.sock]))
        for _ in range(200):
            if self.server.started: break
            await asyncio.sleep(.01)
        self.assertTrue(self.server.started)

    async def asyncTearDown(self):
        # Close app sessions before server shutdown; lifespan repeats this idempotently.
        await self.manager.close()
        self.server.should_exit = True
        await asyncio.wait_for(self.task, 8)
        self.sock.close()
        for s in self.manager.sessions.values():
            if s.pid:
                self.assertTrue(group_absent(s.pid))
                self.assertFalse(Path(f'/proc/{s.pid}').exists())

    async def http(self, path, body=None, headers=None):
        def request():
            req = Request(f'http://127.0.0.1:{self.port}/api/v1'+path,
                          data=json.dumps(body).encode() if body is not None else None,
                          headers={'Content-Type':'application/json', **(headers or {})})
            try:
                with urlopen(req,timeout=8) as response:
                    return response.status,json.load(response)
            except HTTPError as error:
                with error:
                    return error.code,json.load(error)
        return await asyncio.to_thread(request)

    async def create(self, app, **extra):
        code,s = await self.http('/sessions',dict(application_id=app,**extra))
        self.assertEqual(code,201,s)
        return s

    async def terminal(self, identifier):
        for _ in range(500):
            _,s = await self.http('/sessions/'+identifier)
            if s['state'] in {'completed','failed','timed_out','stopped'}: return s
            await asyncio.sleep(.01)
        self.fail('session did not terminate')

    def ws(self, identifier, **kwargs):
        return connect(f'ws://127.0.0.1:{self.port}/ws/v1/sessions/{identifier}', **kwargs)

    async def test_api_validation_and_local_boundary(self):
        self.assertEqual((await self.http('/health'))[1]['status'],'ok')
        self.assertEqual(len((await self.http('/applications'))[1]),3)
        self.assertEqual((await self.http('/sessions',{'application_id':'unknown'}))[0],404)
        self.assertEqual((await self.http('/sessions',{'application_id':'banking_safe','command':'sh'}))[0],422)
        self.assertEqual((await self.http('/sessions/missing'))[0],404)
        self.assertEqual((await self.http('/health',headers={'Origin':'http://evil.example'}))[0],403)
        self.assertEqual((await self.http('/health',headers={'Host':'evil.example'}))[0],403)
        with self.assertRaises(InvalidStatus):
            async with self.ws('missing'): pass
        s = await self.create('banking_safe',auto_start=False)
        with self.assertRaises(InvalidStatus):
            async with self.ws(s['session_id'],origin='http://evil.example'): pass

    async def test_real_safe_unsafe_http_and_live_websockets(self):
        for application,warnings in [('banking_safe',0),('banking_unsafe',1)]:
            s = await self.create(application,auto_start=False); identifier=s['session_id']
            async with self.ws(identifier) as ws, self.ws(identifier) as ws2:
                self.assertEqual(json.loads(await ws.recv())['type'],'session_snapshot')
                await ws2.recv()
                async with self.ws(identifier) as disconnecting:
                    await disconnecting.recv()
                # A disconnected third client cannot terminate the monitored process.
                self.assertEqual((await self.http('/sessions/'+identifier+'/start',{}))[0],200)
                received=[]
                async for message in ws:
                    received.append(json.loads(message))
                real=[m['data']['event'] for m in received if m['type']=='runtime_event']
                second = [json.loads(m) async for m in ws2]
                self.assertEqual([m['data']['event'] for m in second if m['type']=='runtime_event'], real)
                self.assertTrue(real)
                self.assertEqual([m['cursor'] for m in received],sorted({m['cursor'] for m in received}))
            final=await self.terminal(identifier)
            self.assertEqual(final['state'],'completed',final)
            self.assertEqual(final['invalid_records'],0,final)
            self.assertTrue(final['monitoring_complete'],final)
            self.assertTrue(final['cleanup_verified'])
            self.assertEqual(final['warning_count'],warnings)
            _,page=await self.http('/sessions/'+identifier+'/events?limit=1000')
            self.assertEqual([i['event'] for i in page['items']],real)
            _,alerts=await self.http('/sessions/'+identifier+'/alerts')
            self.assertEqual(alerts['alerts'],[e for e in real if e['session_event']=='potential_lock_order_cycle'])
            _,graph=await self.http('/sessions/'+identifier+'/graph')
            self.assertEqual(graph['reported_cycles'],alerts['alerts'])
            evidence_dir = Settings().root / 'build/backend-tests'
            evidence_dir.mkdir(exist_ok=True)
            (evidence_dir / (application+'.json')).write_text(json.dumps(
                {'session': final, 'events': page, 'alerts': alerts, 'graph': graph}, indent=2))
            expected={(h,e['lock_id']) for e in real if e['session_event']=='lock_request' for h in e.get('held_locks',[])}
            self.assertEqual({(e['from_lock'],e['to_lock']) for e in graph['edges']},expected)
            self.assertTrue((await self.http('/sessions/'+identifier+'/threads'))[1]['threads'])
            self.assertEqual((await self.http('/sessions/'+identifier+'/stop',{}))[0],409)
            async with self.ws(identifier) as late:
                snapshot=json.loads(await late.recv())
                self.assertEqual(snapshot['data']['session']['state'],'completed')
                self.assertEqual(snapshot['data']['events']['total_received'],final['event_count'])
            print('REAL_API',application,json.dumps(final), flush=True)

    async def test_deadlock_late_snapshot_timeout_and_stop(self):
        s=await self.create('banking_deadlock'); identifier=s['session_id']
        await asyncio.sleep(.2)
        async with self.ws(identifier) as ws:
            snapshot=json.loads(await ws.recv())
            self.assertGreater(snapshot['data']['events']['total_received'],0)
            messages=[json.loads(m) async for m in ws]
            self.assertTrue(any(m['type']=='session_status' and m['data']['state']=='timed_out' for m in messages))
        final=await self.terminal(identifier)
        self.assertEqual(final['state'],'timed_out')
        self.assertTrue(final['controlled_deadlock_evidence']['fixture_verified'],final)
        self.assertTrue(final['cleanup_verified'])
        print('REAL_DEADLOCK',json.dumps(final),flush=True)
        s=await self.create('banking_deadlock')
        await asyncio.sleep(.1)
        code,final=await self.http('/sessions/'+s['session_id']+'/stop',{})
        self.assertEqual(code,200)
        self.assertEqual(final['state'],'stopped')
        self.assertTrue(final['cleanup_verified'])

    async def test_limits_history_shutdown_and_descriptor_cleanup(self):
        baseline=len(os.listdir('/proc/self/fd'))
        manager=SessionManager(Settings(max_concurrent=1,max_sessions=3,max_events=3))
        first=manager.create(SessionCreate(application_id='banking_deadlock'))
        with self.assertRaises(Exception) as caught:
            manager.create(SessionCreate(application_id='banking_safe'))
        self.assertEqual(caught.exception.status_code,429)
        await asyncio.sleep(.1)
        await manager.close()
        self.assertEqual(first.state,'stopped'); self.assertTrue(first.cleanup_verified)
        manager=SessionManager(Settings(max_sessions=2,max_events=3))
        for _ in range(2):
            s=manager.create(SessionCreate(application_id='banking_safe'))
            await s.task
            self.assertEqual(s.state,'completed')
            self.assertTrue(s.page()['gap']); self.assertEqual(len(s.events),3)
            self.assertTrue(s.monitoring_complete)
        with self.assertRaises(Exception) as caught:
            manager.create(SessionCreate(application_id='banking_safe'))
        self.assertEqual(caught.exception.status_code,429)
        await manager.close(); await asyncio.sleep(.05)
        self.assertLessEqual(len(os.listdir('/proc/self/fd')),baseline)

    async def test_missing_dependencies_empty_stream_and_launch_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); manager=SessionManager(Settings(root=root))
            with self.assertRaises(Exception) as caught:
                manager.create(SessionCreate(application_id='banking_safe'))
            self.assertEqual(caught.exception.status_code,503)
            # Explicit negative unit fixture: executable clears loader state by being a shell builtin exit.
            (root/'build').mkdir()
            import shutil
            shutil.copyfile(Settings().root/'build/libdeadlock_monitor.so',root/'build/libdeadlock_monitor.so')
            exe=root/'build/banking_safe'
            exe.write_text('not a valid executable\n'); exe.chmod(0o700)
            s=manager.create(SessionCreate(application_id='banking_safe')); await s.task
            self.assertEqual(s.state,'failed'); self.assertTrue(s.cleanup_verified)
            # /bin/true is a real process but emits session metadata; an empty stream is tested via a disabled monitor library.
            shutil.copyfile('/bin/true',exe)
            (root/'build/libdeadlock_monitor.so').write_bytes(b'not an ELF library')
            s=manager.create(SessionCreate(application_id='banking_safe')); await s.task
            self.assertEqual(s.state,'failed'); self.assertEqual(s.error,'no_monitor_events')
            self.assertTrue(s.cleanup_verified)

    async def test_real_server_shutdown_cleans_active_child(self):
        s = await self.create('banking_deadlock')
        await asyncio.sleep(.15)
        self.server.should_exit = True
        await asyncio.wait_for(self.task, 8)
        internal = self.manager.get(s['session_id'])
        self.assertEqual(internal.state, 'stopped')
        self.assertTrue(internal.cleanup_verified)
        self.assertTrue(group_absent(internal.pid))

    async def test_slow_live_client_queue_recovers_with_snapshot(self):
        # Deliberately do not drain this subscriber: real monitor events fill its queue.
        s = await self.create('banking_safe', auto_start=False)
        internal = self.manager.get(s['session_id'])
        internal.channel.queue_size = 1
        subscriber = internal.channel.subscribe()
        await self.http('/sessions/'+s['session_id']+'/start', {})
        final = await self.terminal(s['session_id'])
        self.assertEqual(final['state'], 'completed')
        self.assertTrue(subscriber.overflow)
        self.assertEqual(subscriber.queue.qsize(), 1)
        internal.channel.unsubscribe(subscriber)
        async with self.ws(s['session_id']) as ws:
            snapshot = json.loads(await ws.recv())
            self.assertEqual(snapshot['data']['events']['total_received'], final['event_count'])


if __name__ == '__main__':
    unittest.main()
