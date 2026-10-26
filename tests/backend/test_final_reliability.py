"""Additional Phase 7 lifecycle checks; every launched child is real and bounded."""
import asyncio
import os
from pathlib import Path
import signal
import unittest
from app.config import Settings
from app.models import SessionCreate
from app.session_manager import SessionManager
from app.process_runner import group_absent


class FinalReliability(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.manager = SessionManager(Settings(max_sessions=30))

    async def asyncTearDown(self):
        await self.manager.close()
        for session in self.manager.sessions.values():
            if session.pid:
                self.assertTrue(group_absent(session.pid))
                self.assertFalse(Path(f'/proc/{session.pid}').exists())

    async def running(self):
        s = self.manager.create(SessionCreate(application_id='banking_deadlock'))
        for _ in range(200):
            if s.pid and s.event_count: return s
            await asyncio.sleep(.01)
        self.fail('fixture did not start')

    async def test_cancelled_supervisor_is_terminal_and_reaped(self):
        s = await self.running()
        s.task.cancel()
        with self.assertRaises(asyncio.CancelledError): await s.task
        self.assertEqual(s.state, 'stopped')
        self.assertEqual(s.error, 'supervisor_cancelled')
        self.assertTrue(s.cleanup_verified)
        self.assertEqual(self.manager.active(), 0)

    async def test_child_abnormal_exit_is_failed_not_completed(self):
        s = await self.running()
        os.kill(s.pid, signal.SIGKILL)  # deterministic crash-like exit; no core dump
        await s.task
        self.assertEqual(s.state, 'failed')
        self.assertEqual(s.exit_code, -signal.SIGKILL)
        self.assertFalse(s.ingestion_complete)
        self.assertTrue(s.cleanup_verified)

    async def test_two_concurrent_slots_limit_and_repeated_runs(self):
        first, second = await self.running(), await self.running()
        self.assertNotEqual(first.pid, second.pid)
        with self.assertRaises(Exception) as caught:
            self.manager.create(SessionCreate(application_id='banking_safe'))
        self.assertEqual(caught.exception.status_code, 429)
        await asyncio.gather(self.manager.stop(first), self.manager.stop(second))
        baseline = len(os.listdir('/proc/self/fd'))
        for index in range(12):
            s = self.manager.create(SessionCreate(application_id='banking_safe' if index % 2 == 0 else 'banking_unsafe'))
            await s.task
            self.assertEqual(s.state, 'completed')
            self.assertEqual(s.projection.warning_count, index % 2)
            self.assertTrue(s.monitoring_complete)
            self.assertTrue(s.cleanup_verified)
        await asyncio.sleep(.05)
        self.assertLessEqual(len(os.listdir('/proc/self/fd')), baseline)

    async def test_new_backend_manager_has_no_old_sessions(self):
        s = self.manager.create(SessionCreate(application_id='banking_safe'))
        await s.task
        await self.manager.close()
        restarted = SessionManager(Settings())
        with self.assertRaises(Exception) as caught: restarted.get(s.session_id)
        self.assertEqual(caught.exception.status_code, 404)
        self.assertEqual(restarted.sessions, {})
