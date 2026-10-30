"""Real six-lock workloads through the same supervisor used by the dashboard."""
import asyncio
import unittest
from app.config import Settings
from app.models import SessionCreate
from app.session_manager import SessionManager


class NetworkIntegration(unittest.IsolatedAsyncioTestCase):
    async def test_six_lock_networks(self):
        manager = SessionManager(Settings())
        try:
            for application, warnings in [('network_safe', 0), ('network_cycle', 1)]:
                with self.subTest(application=application):
                    session = manager.create(SessionCreate(application_id=application))
                    await asyncio.wait_for(session.task, timeout=10)
                    self.assertEqual(session.state, 'completed')
                    self.assertEqual(session.exit_code, 0)
                    self.assertTrue(session.cleanup_verified)
                    self.assertTrue(session.monitoring_complete)
                    self.assertEqual(len(session.projection.nodes), 6)
                    self.assertEqual(len(session.projection.edges), 6)
                    self.assertEqual(len(session.projection.threads), 6)
                    self.assertEqual(session.projection.warning_count, warnings)
                    output = session.view()['stdout']
                    self.assertEqual(output.count('TRANSFER worker='), 18)
                    self.assertIn('TOTAL cents=600000 transfers=18 VALIDATION PASS', output)
                    if warnings:
                        cycle = session.projection.alerts[0]['cycle']
                        self.assertEqual(len(cycle), 7)
                        self.assertEqual(cycle[0], cycle[-1])
                        self.assertEqual(len(set(cycle)), 6)
        finally:
            await manager.close()
