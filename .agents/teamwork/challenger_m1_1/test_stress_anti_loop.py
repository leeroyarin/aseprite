#!/usr/bin/env python3
"""
Empirical Stress Test Harness: Anti-Loop Guard & Task Lifecycle State Transitions
Challenger: Challenger 1 (Milestone 1)
Target: tools/agent-mesh/mesh_subagent.py
"""

import concurrent.futures
import json
import os
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch, MagicMock

# Locate repo root and mesh_subagent
REPO_ROOT = Path(__file__).resolve().parents[3]
AGENT_MESH_DIR = REPO_ROOT / "tools" / "agent-mesh"
sys.path.insert(0, str(AGENT_MESH_DIR))

import mesh_subagent


class StressTestAntiLoopAndTaskStore(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.fake_processed_file = Path(self.temp_dir.name) / "test_mesh_tasks.json"
        self.orig_processed_file = mesh_subagent.PROCESSED_FILE
        mesh_subagent.PROCESSED_FILE = self.fake_processed_file

        self.cfg = mesh_subagent.Config(
            hub_url="https://mock-hub.local",
            agent="test-challenger-agent",
            token="amt_mock_token_1234567890abcdef",
            projects=["agent-hub"]
        )
        self.client = mesh_subagent.MeshClient(self.cfg)

    def tearDown(self):
        mesh_subagent.PROCESSED_FILE = self.orig_processed_file
        self.temp_dir.cleanup()

    # =========================================================================
    # Suite 1: Sequential Anti-Loop Guard Stress
    # =========================================================================
    def test_01_sequential_duplicate_acks(self):
        """Stress-test sequential duplicate send_ack calls (10 attempts)."""
        task_id = "seq-anti-loop-task-001"
        http_calls = []

        def mock_send(to, body, msg_type="result", reply_to=None, project=None):
            msg_id = f"mock-msg-{len(http_calls) + 1}"
            http_calls.append({"to": to, "body": body, "reply_to": reply_to, "id": msg_id})
            return {"ok": True, "id": msg_id}

        with patch.object(self.client, "send_message", side_effect=mock_send):
            # Attempt 1: First ACK
            res1 = self.client.send_ack(task_id, status="accepted", note="First ACK attempt")
            self.assertTrue(res1.get("ok"))
            self.assertEqual(res1.get("id"), "mock-msg-1")
            self.assertFalse(res1.get("skipped", False))
            self.assertEqual(len(http_calls), 1)

            # Attempts 2 to 10: Repeated ACKs without force
            for i in range(2, 11):
                res = self.client.send_ack(task_id, status="accepted", note=f"Duplicate ACK {i}")
                self.assertTrue(res.get("ok"))
                self.assertTrue(res.get("skipped"), f"Call {i} was not skipped!")
                self.assertEqual(len(http_calls), 1, f"HTTP call count increased on attempt {i}!")

        # Verify state file on disk
        rec = mesh_subagent.TaskStore.get_record(task_id)
        self.assertIsNotNone(rec)
        self.assertEqual(rec["status"], "ACKNOWLEDGED")
        self.assertTrue(rec.get("ack_sent"))
        self.assertEqual(rec.get("ack_message_id"), "mock-msg-1")

    def test_02_send_ack_when_in_progress_or_completed(self):
        """Verify send_ack is skipped if task is IN_PROGRESS or COMPLETED."""
        task_in_prog = "task-in-progress-001"
        mesh_subagent.TaskStore.record_state(task_in_prog, "IN_PROGRESS", {"branch": "feat/xyz"})

        task_completed = "task-completed-001"
        mesh_subagent.TaskStore.record_state(task_completed, "COMPLETED", {"sha": "123456"})

        with patch.object(self.client, "send_message") as mock_send:
            res_prog = self.client.send_ack(task_in_prog, status="accepted")
            self.assertTrue(res_prog.get("skipped"))
            mock_send.assert_not_called()

            res_comp = self.client.send_ack(task_completed, status="accepted")
            self.assertTrue(res_comp.get("skipped"))
            mock_send.assert_not_called()

    def test_03_send_ack_with_force_override(self):
        """Verify that force=True overrides anti-loop guard when explicitly requested."""
        task_id = "task-forced-ack-001"
        with patch.object(self.client, "send_message") as mock_send:
            mock_send.side_effect = [
                {"ok": True, "id": "ack-1"},
                {"ok": True, "id": "ack-2"}
            ]
            res1 = self.client.send_ack(task_id, status="accepted", note="Initial")
            self.assertEqual(res1["id"], "ack-1")
            self.assertFalse(res1.get("skipped", False))

            # Normal duplicate: skipped
            res2 = self.client.send_ack(task_id, status="accepted", note="Duplicate")
            self.assertTrue(res2.get("skipped"))
            self.assertEqual(mock_send.call_count, 1)

            # Forced duplicate: permitted
            res3 = self.client.send_ack(task_id, status="accepted", note="Forced Override", force=True)
            self.assertEqual(res3["id"], "ack-2")
            self.assertFalse(res3.get("skipped", False))
            self.assertEqual(mock_send.call_count, 2)

    # =========================================================================
    # Suite 2: Concurrency & Parallel Execution Stress
    # =========================================================================
    def test_04_concurrent_acks_on_already_acknowledged_task(self):
        """Stress-test 20 concurrent threads attempting send_ack on an already-ACKed task."""
        task_id = "concurrent-acked-task-001"
        mesh_subagent.TaskStore.record_state(task_id, "ACKNOWLEDGED", {"ack_message_id": "existing-ack-id"})

        with patch.object(self.client, "send_message") as mock_send:
            def try_ack(thread_idx):
                return self.client.send_ack(task_id, status="accepted", note=f"Thread {thread_idx}")

            with concurrent.futures.ThreadPoolExecutor(max_workers=10) as executor:
                futures = [executor.submit(try_ack, i) for i in range(20)]
                results = [f.result() for f in concurrent.futures.as_completed(futures)]

            for r in results:
                self.assertTrue(r.get("skipped"))
            mock_send.assert_not_called()

    def test_05_concurrent_acks_on_unacknowledged_task_race_condition(self):
        """
        Adversarial challenge: Test whether concurrent send_ack calls on an unacknowledged
        task exhibit TOCTOU race conditions when network latency is simulated.
        """
        task_id = "concurrent-unacked-task-001"
        http_call_count = 0
        call_lock = concurrent.futures.thread.threading.Lock()

        def slow_send(to, body, msg_type="result", reply_to=None, project=None):
            nonlocal http_call_count
            with call_lock:
                http_call_count += 1
                current_id = f"msg-{http_call_count}"
            time.sleep(0.02)  # Simulate 20ms network latency
            return {"ok": True, "id": current_id}

        with patch.object(self.client, "send_message", side_effect=slow_send):
            def try_ack(idx):
                return self.client.send_ack(task_id, status="accepted", note=f"Race thread {idx}")

            with concurrent.futures.ThreadPoolExecutor(max_workers=5) as executor:
                futures = [executor.submit(try_ack, i) for i in range(5)]
                results = [f.result() for f in concurrent.futures.as_completed(futures)]

            print(f"\n[Empirical Concurrency Observation] 5 concurrent send_ack calls resulted in {http_call_count} HTTP requests.")
            # Note: Document whether http_call_count > 1 under unsynchronized concurrent arrival

    # =========================================================================
    # Suite 3: TaskStore Lifecycle & Downgrade Immunity Stress
    # =========================================================================
    def test_06_lifecycle_forward_transitions(self):
        """Test strict monotonic forward transitions: NEW -> ACKNOWLEDGED -> IN_PROGRESS -> COMPLETED."""
        task_id = "lifecycle-task-100"

        # 0. Initial state
        self.assertFalse(mesh_subagent.TaskStore.is_acknowledged(task_id))
        self.assertFalse(mesh_subagent.TaskStore.is_in_progress(task_id))
        self.assertFalse(mesh_subagent.TaskStore.is_processed(task_id))
        self.assertIsNone(mesh_subagent.TaskStore.get_record(task_id))

        # 1. Transition to NEW
        mesh_subagent.TaskStore.record_new_task(
            task_id=task_id,
            sender="muse",
            timestamp="2026-11-03T04:00:00Z",
            body_snippet="Phase 1 silhouette carving",
            subject="Phase 1",
            body="Phase 1 silhouette carving full directive"
        )
        rec_new = mesh_subagent.TaskStore.get_record(task_id)
        self.assertEqual(rec_new["status"], "NEW")
        self.assertEqual(rec_new["sender"], "muse")
        self.assertFalse(mesh_subagent.TaskStore.is_acknowledged(task_id))
        self.assertFalse(mesh_subagent.TaskStore.is_in_progress(task_id))
        self.assertFalse(mesh_subagent.TaskStore.is_processed(task_id))

        # 2. Transition to ACKNOWLEDGED
        mesh_subagent.TaskStore.record_state(task_id, "ACKNOWLEDGED", {"ack_message_id": "msg-ack-100"})
        rec_ack = mesh_subagent.TaskStore.get_record(task_id)
        self.assertEqual(rec_ack["status"], "ACKNOWLEDGED")
        self.assertTrue(rec_ack["ack_sent"])
        self.assertTrue(mesh_subagent.TaskStore.is_acknowledged(task_id))
        self.assertFalse(mesh_subagent.TaskStore.is_in_progress(task_id))
        self.assertFalse(mesh_subagent.TaskStore.is_processed(task_id))

        # 3. Transition to IN_PROGRESS
        mesh_subagent.TaskStore.record_state(task_id, "IN_PROGRESS", {"branch": "feat/daedalus-1"})
        rec_prog = mesh_subagent.TaskStore.get_record(task_id)
        self.assertEqual(rec_prog["status"], "IN_PROGRESS")
        self.assertTrue(mesh_subagent.TaskStore.is_acknowledged(task_id))
        self.assertTrue(mesh_subagent.TaskStore.is_in_progress(task_id))
        self.assertFalse(mesh_subagent.TaskStore.is_processed(task_id))

        # 4. Transition to COMPLETED
        mesh_subagent.TaskStore.record_state(task_id, "COMPLETED", {"sha": "b0f0578a83"})
        rec_comp = mesh_subagent.TaskStore.get_record(task_id)
        self.assertEqual(rec_comp["status"], "COMPLETED")
        self.assertTrue(mesh_subagent.TaskStore.is_acknowledged(task_id))
        self.assertFalse(mesh_subagent.TaskStore.is_in_progress(task_id))
        self.assertTrue(mesh_subagent.TaskStore.is_processed(task_id))

    def test_07_downgrade_immunity_to_new(self):
        """
        Verify that attempting to downgrade an ACKNOWLEDGED, IN_PROGRESS, or COMPLETED
        task to NEW (via record_new_task or record_state) fails and preserves status.
        """
        for state in ("ACKNOWLEDGED", "IN_PROGRESS", "COMPLETED"):
            tid = f"downgrade-test-{state.lower()}"
            mesh_subagent.TaskStore.record_state(tid, state, {"init_field": "preserved"})

            # Attempt 1: record_new_task
            mesh_subagent.TaskStore.record_new_task(
                task_id=tid,
                sender="muse",
                body="Attempting to re-ingest as NEW"
            )
            rec = mesh_subagent.TaskStore.get_record(tid)
            self.assertEqual(rec["status"], state, f"State {state} was downgraded by record_new_task!")
            self.assertEqual(rec.get("init_field"), "preserved")

            # Attempt 2: record_state(tid, "NEW")
            mesh_subagent.TaskStore.record_state(tid, "NEW", {"malicious": "overwrite"})
            rec2 = mesh_subagent.TaskStore.get_record(tid)
            self.assertEqual(rec2["status"], state, f"State {state} was downgraded by record_state(NEW)!")
            self.assertEqual(rec2.get("init_field"), "preserved")

    def test_08_downgrade_from_completed_to_acknowledged_or_inprogress(self):
        """
        Adversarial challenge: Check whether COMPLETED status can be altered
        to ACKNOWLEDGED or IN_PROGRESS via record_state, and check is_processed protection.
        """
        tid = "completed-task-downgrade-check"
        mesh_subagent.TaskStore.record_state(tid, "COMPLETED", {"sha": "final123"})
        self.assertTrue(mesh_subagent.TaskStore.is_processed(tid))

        # Call record_state(tid, "IN_PROGRESS")
        mesh_subagent.TaskStore.record_state(tid, "IN_PROGRESS")
        rec = mesh_subagent.TaskStore.get_record(tid)
        # Even if rec['status'] became IN_PROGRESS, is_processed must still return True
        # because the task ID is in data['processed']!
        self.assertTrue(mesh_subagent.TaskStore.is_processed(tid), "is_processed lost for completed task!")
        print(f"\n[Observation] When record_state('IN_PROGRESS') called on COMPLETED: rec['status']={rec.get('status')}, is_processed={mesh_subagent.TaskStore.is_processed(tid)}")

    # =========================================================================
    # Suite 4: Progress Reporting Decoupling Stress
    # =========================================================================
    def test_09_progress_reporting_decoupling(self):
        """Verify send_progress does not mutate task status to ACKNOWLEDGED or block send_ack."""
        tid = "progress-decouple-task-001"
        mesh_subagent.TaskStore.record_new_task(tid, sender="muse", body="Directive requiring check-ins")

        with patch.object(self.client, "send_message", return_value={"ok": True, "id": "prog-msg-1"}) as mock_send:
            # Send 5 progress updates
            for i in range(1, 6):
                res = self.client.send_progress(tid, note=f"Progress step {i} / 5")
                self.assertEqual(res["id"], "prog-msg-1")

            self.assertEqual(mock_send.call_count, 5)

        # Confirm status is STILL NEW on disk
        rec = mesh_subagent.TaskStore.get_record(tid)
        self.assertEqual(rec["status"], "NEW")
        self.assertFalse(mesh_subagent.TaskStore.is_acknowledged(tid))
        self.assertEqual(rec["last_progress_note"], "Progress step 5 / 5")

        # Now send ACK: must NOT be blocked by the 5 prior progress messages
        with patch.object(self.client, "send_message", return_value={"ok": True, "id": "ack-msg-real"}) as mock_ack:
            ack_res = self.client.send_ack(tid, status="accepted", note="Starting actual task work")
            self.assertEqual(ack_res["id"], "ack-msg-real")
            self.assertFalse(ack_res.get("skipped", False))
            self.assertEqual(mock_ack.call_count, 1)

        rec_after = mesh_subagent.TaskStore.get_record(tid)
        self.assertEqual(rec_after["status"], "ACKNOWLEDGED")
        self.assertTrue(mesh_subagent.TaskStore.is_acknowledged(tid))

    # =========================================================================
    # Suite 5: Corrupted State & Edge Case Resilience
    # =========================================================================
    def test_10_corrupted_state_file_recovery(self):
        """Stress test recovery against corrupted or empty JSON files."""
        corrupted_payloads = [
            "",
            "   \n  \t ",
            "{\"unclosed: [",
            "12345",
            "\"string value\"",
            "true",
            "[]",
            "{\"processed\": null, \"records\": null}"
        ]
        for payload in corrupted_payloads:
            self.fake_processed_file.write_text(payload, encoding="utf-8")
            data = mesh_subagent.TaskStore.load()
            self.assertIsInstance(data, dict)
            self.assertIsInstance(data.get("processed"), list)
            self.assertIsInstance(data.get("records"), dict)
            self.assertFalse(mesh_subagent.TaskStore.is_acknowledged("any-id"))
            self.assertFalse(mesh_subagent.TaskStore.is_processed("any-id"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
