#!/usr/bin/env python3
"""
Unit and Integration Tests for Mesh Subagent (tools/agent-mesh/mesh_subagent.py).
Tests:
1. Config initialization and fallback
2. TaskStore state management and deduplication
3. Anti-loop duplicate ACK prevention
4. TaskParser (standard directives and variations)
5. BuildAndTestRunner.parse_test_output (Lua summary lines and bracket progress parsing)
6. GitOps hygiene checks:
   - Token detection in diff
   - Token detection in untracked files
   - third_party detection (tracked and untracked)
   - PixelLab detection (tracked and untracked)
   - Line length enforcement strictly on added lines (no false positives on pre-existing code)
7. Single ACK message formatting
8. HandoffManager log entry insertion
9. GitOps.sync operations
10. Inbox filtering of noise / already-processed tasks
"""

import argparse
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch, MagicMock

# Ensure tools/agent-mesh is on sys.path
SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import mesh_subagent


class TestMeshSubagent(unittest.TestCase):
    def setUp(self):
        self.test_dir = tempfile.TemporaryDirectory()
        self.fake_processed = Path(self.test_dir.name) / "test_processed.json"
        # Patch PROCESSED_FILE in mesh_subagent
        self.orig_processed_file = mesh_subagent.PROCESSED_FILE
        mesh_subagent.PROCESSED_FILE = self.fake_processed

    def tearDown(self):
        mesh_subagent.PROCESSED_FILE = self.orig_processed_file
        self.test_dir.cleanup()

    def test_config_loading(self):
        with patch.dict(os.environ, {
            "HUB_URL": "https://fake-hub.example.com",
            "AGENT_NAME": "test-agent",
            "AGENT_TOKEN": "amt_" + "test_token_12345",
            "PROJECTS": "agent-hub,pitch-quest"
        }):
            cfg = mesh_subagent.Config.load()
            self.assertEqual(cfg.hub_url, "https://fake-hub.example.com")
            self.assertEqual(cfg.agent, "test-agent")
            self.assertEqual(cfg.token, "amt_" + "test_token_12345")
            self.assertEqual(cfg.projects, ["agent-hub", "pitch-quest"])
            self.assertEqual(cfg.primary_project, "agent-hub")

    def test_task_store_lifecycle(self):
        task_id = "task-uuid-1234"
        self.assertFalse(mesh_subagent.TaskStore.is_processed(task_id))
        self.assertFalse(mesh_subagent.TaskStore.is_acknowledged(task_id))

        # Record ACK
        mesh_subagent.TaskStore.record_state(task_id, "ACKNOWLEDGED", {"ack_note": "accepted"})
        rec = mesh_subagent.TaskStore.get_record(task_id)
        self.assertIsNotNone(rec)
        self.assertEqual(rec["status"], "ACKNOWLEDGED")
        self.assertTrue(mesh_subagent.TaskStore.is_acknowledged(task_id))
        self.assertFalse(mesh_subagent.TaskStore.is_processed(task_id))

        # Record In Progress
        mesh_subagent.TaskStore.record_state(task_id, "IN_PROGRESS", {"branch": "feature/test"})
        rec = mesh_subagent.TaskStore.get_record(task_id)
        self.assertEqual(rec["status"], "IN_PROGRESS")
        self.assertEqual(rec["branch"], "feature/test")
        self.assertTrue(mesh_subagent.TaskStore.is_acknowledged(task_id))
        self.assertTrue(mesh_subagent.TaskStore.is_in_progress(task_id))
        self.assertFalse(mesh_subagent.TaskStore.is_processed(task_id))

        # Record Completed
        mesh_subagent.TaskStore.record_state(task_id, "COMPLETED", {"sha": "abcdef1234"})
        self.assertTrue(mesh_subagent.TaskStore.is_processed(task_id))
        self.assertTrue(mesh_subagent.TaskStore.is_acknowledged(task_id))
        rec = mesh_subagent.TaskStore.get_record(task_id)
        self.assertEqual(rec["status"], "COMPLETED")
        self.assertEqual(rec["sha"], "abcdef1234")

    def test_anti_loop_ack_duplicate_prevention(self):
        cfg = mesh_subagent.Config(
            hub_url="https://fake-hub.example.com",
            agent="test-node",
            token="fake-token",
            projects=["agent-hub"]
        )
        client = mesh_subagent.MeshClient(cfg)
        task_id = "task-anti-loop-1"

        with patch.object(client, "send_message", return_value={"ok": True, "id": "msg-ack-1"}) as mock_send:
            # First ACK: should dispatch message
            res1 = client.send_ack(task_id, status="accepted", note="Initial ACK")
            self.assertEqual(res1["id"], "msg-ack-1")
            self.assertEqual(mock_send.call_count, 1)

            # Second ACK: should be skipped per anti-loop policy
            res2 = client.send_ack(task_id, status="accepted", note="Duplicate ACK attempt")
            self.assertTrue(res2.get("skipped"))
            # Call count remains 1 (no message sent to hub)
            self.assertEqual(mock_send.call_count, 1)

            # Third ACK with force=True: should dispatch message
            mock_send.return_value = {"ok": True, "id": "msg-ack-2"}
            res3 = client.send_ack(task_id, status="accepted", note="Forced ACK", force=True)
            self.assertEqual(res3["id"], "msg-ack-2")
            self.assertEqual(mock_send.call_count, 2)

    def test_task_parser_muse_directive_standard(self):
        body = (
            "TASK — Phase 1: Easymetric Base-first silhouette (proceeding from completed Phase 0)\n"
            "1) Checkout: Aseprite-2.0 at exactly b0f0578. Create a NEW branch off it "
            "(suggested: feature/easymetric-base-first-daedalus).\n"
            "2) Implement Phase 1..."
        )
        parsed = mesh_subagent.TaskParser.parse_muse_task(body)
        self.assertEqual(parsed["branch"], "feature/easymetric-base-first-daedalus")
        self.assertEqual(parsed["base"], "b0f0578")
        self.assertIn("Phase 1", parsed["phase"])

    def test_task_parser_muse_directive_variations(self):
        body1 = "TASK: Phase 2 Widget Redesign\nBranch: `feature/widget-refresh`\nCheckout: Aseprite-2.0 at a1b2c3d4e5."
        parsed1 = mesh_subagent.TaskParser.parse_muse_task(body1)
        self.assertEqual(parsed1["branch"], "feature/widget-refresh")
        self.assertEqual(parsed1["base"], "a1b2c3d4e5")

        body2 = "Phase 3: Exporter fixes\nBase commit: deadbeef12\nbranch feature/exporter-fix"
        parsed2 = mesh_subagent.TaskParser.parse_muse_task(body2)
        self.assertEqual(parsed2["branch"], "feature/exporter-fix")
        self.assertEqual(parsed2["base"], "deadbeef12")

    def test_parse_test_output_pass(self):
        sample_output = """
        ======================================================
         Easymetric Live Studio E2E Test Suite Runner        
        ======================================================
        TOTAL:   67 Passed,   0 Failed (Total 67)
        ======================================================
        Test execution finished in 8.74 seconds with exit code: 0
        SUCCESS: All Easymetric E2E tests passed.
        """
        parsed = mesh_subagent.BuildAndTestRunner.parse_test_output(sample_output, "")
        self.assertEqual(parsed["passed"], 67)
        self.assertEqual(parsed["failed"], 0)
        self.assertEqual(parsed["duration_s"], 8.74)
        self.assertEqual(len(parsed["failures"]), 0)

    def test_parse_test_output_fail(self):
        sample_output = """
        ======================================================
         Easymetric Live Studio E2E Test Suite Runner        
        ======================================================
        TOTAL:   21 Passed,   2 Failed (Total 23)
        ======================================================
        [TC 4.2] Opposing faces paired dimensions: FAIL (expected 32, got 48)
        [TC 4.3] Column lock propagation: FAIL (assert error at line 850)
        
        Test execution finished in 4.53 seconds with exit code: 1
        FAILURE: Easymetric E2E tests failed with exit code 1.
        """
        parsed = mesh_subagent.BuildAndTestRunner.parse_test_output(sample_output, "")
        self.assertEqual(parsed["passed"], 21)
        self.assertEqual(parsed["failed"], 2)
        self.assertEqual(parsed["duration_s"], 4.53)
        self.assertTrue(len(parsed["failures"]) >= 2)

    def test_parse_test_output_bracket_fail_regression(self):
        # Critical regression test: previous version mistook [2/2] as 2 passed out of 2
        sample_output = (
            "[1/2] Surface layout initialization: FAIL (out of bounds)\n"
            "[2/2] Wavefront OBJ export: PASS\n"
        )
        parsed = mesh_subagent.BuildAndTestRunner.parse_test_output(sample_output, "")
        self.assertEqual(parsed["passed"], 1)
        self.assertEqual(parsed["failed"], 1)
        self.assertTrue(any("out of bounds" in f for f in parsed["failures"]))

    def test_format_final_report(self):
        build_res = {
            "exit_code": 0,
            "duration_s": 12.5,
            "bin_exists": True,
        }
        suites = {
            "suites": {
                "regression": {"script": "tests/easymetric_test.lua", "passed": 67, "failed": 0, "duration_s": 8.7, "success": True},
                "adversarial": {"script": "tests/easymetric_adversarial_stress.lua", "passed": 54, "failed": 0, "duration_s": 3.9, "success": True},
                "four_req": {"script": "tests/easymetric_four_requirements_test.lua", "passed": 23, "failed": 0, "duration_s": 4.1, "success": True},
            }
        }
        report = mesh_subagent.TaskParser.format_final_report(
            branch="feature/easymetric-base-first-daedalus",
            commit_sha="b0f0578a83",
            build_res=build_res,
            suite_results=suites,
            pr_url="https://github.com/leeroyarin/aseprite/pull/7"
        )
        self.assertIn("RESULT — Verification & Task Report for branch 'feature/easymetric-base-first-daedalus'", report)
        self.assertIn("Commit SHA: b0f0578a83", report)
        self.assertIn("Exit Code:  0", report)
        self.assertIn("67 passed, 0 failed", report)
        self.assertIn("54 passed, 0 failed", report)
        self.assertIn("23 passed, 0 failed", report)
        self.assertIn("https://github.com/leeroyarin/aseprite/pull/7", report)

    def test_single_ack_format(self):
        cfg = mesh_subagent.Config(
            hub_url="https://fake-hub.example.com",
            agent="test-node",
            token="fake-token",
            projects=["agent-hub"]
        )
        client = mesh_subagent.MeshClient(cfg)

        with patch.object(client, "send_message", return_value={"ok": True, "id": "msg-ack-1"}) as mock_send:
            res = client.send_ack("task-123", status="accepted", note="Starting work on Phase 1")
            self.assertEqual(res["id"], "msg-ack-1")
            mock_send.assert_called_once_with(
                to="muse",
                body="[accepted] Starting work on Phase 1",
                msg_type="status",
                reply_to="task-123",
                project="agent-hub"
            )
            rec = mesh_subagent.TaskStore.get_record("task-123")
            self.assertEqual(rec["status"], "ACKNOWLEDGED")

    def test_handoff_manager_insertion(self):
        fake_handoff = Path(self.test_dir.name) / "HANDOFF.md"
        fake_handoff.write_text(
            "# Aseprite 2.0 — HANDOFF\n\n"
            "## Environment facts\n- facts\n\n"
            "## Log (newest first)\n"
            "- 2026-11-02 — Older entry: Old stuff.\n",
            encoding="utf-8"
        )
        with patch.object(mesh_subagent, "REPO_ROOT", Path(self.test_dir.name)):
            target_file = Path(self.test_dir.name) / "docs" / "aseprite2" / "HANDOFF.md"
            target_file.parent.mkdir(parents=True, exist_ok=True)
            target_file.write_text(fake_handoff.read_text(encoding="utf-8"), encoding="utf-8")

            success = mesh_subagent.HandoffManager.prepend_log_entry(
                summary="New subagent verified baseline",
                details=["Build exit 0", "All tests passed"],
                agent_label="Daedalus (Subagent)"
            )
            self.assertTrue(success)
            content = target_file.read_text(encoding="utf-8")
            self.assertIn("Daedalus (Subagent): New subagent verified baseline", content)
            self.assertIn("Build exit 0", content)
            self.assertIn("Older entry", content)

    def test_hygiene_audit_token_leak_in_diff(self):
        dummy_token = "amt_" + "abcdef0123456789abcdef0123456789"
        with patch.object(mesh_subagent.GitOps, "check_diff", return_value=(0, "", "")):
            with patch.object(mesh_subagent, "run_cmd") as mock_run:
                mock_run.side_effect = [
                    (0, "src/app/test.cpp", ""),  # git diff --name-only
                    (0, "", ""),                  # git status --porcelain
                    (0, f"+ const char* token = \"{dummy_token}\";", ""),  # git diff HEAD
                    (0, "", "")                   # git diff -U0 HEAD -- src/
                ]
                clean, issues = mesh_subagent.GitOps.audit_hygiene()
                self.assertFalse(clean)
                self.assertTrue(any("secret token" in i.lower() for i in issues))

    def test_hygiene_audit_untracked_token_leak(self):
        dummy_token = "amt_" + "9876543210abcdef9876543210fedcba"
        with patch.object(mesh_subagent.GitOps, "check_diff", return_value=(0, "", "")):
            with patch.object(mesh_subagent, "run_cmd") as mock_run:
                # Untracked file containing token
                untracked_file = Path(self.test_dir.name) / "untracked_secret.txt"
                untracked_file.write_text(f"token = {dummy_token}")

                with patch.object(mesh_subagent, "REPO_ROOT", Path(self.test_dir.name)):
                    mock_run.side_effect = [
                        (0, "", ""),                           # git diff --name-only
                        (0, "?? untracked_secret.txt", ""),    # git status --porcelain
                        (0, "", ""),                           # git diff HEAD
                        (0, "", "")                            # git diff -U0 HEAD -- src/
                    ]
                    clean, issues = mesh_subagent.GitOps.audit_hygiene()
                    self.assertFalse(clean)
                    self.assertTrue(any("untracked file" in i.lower() for i in issues))

    def test_hygiene_audit_third_party_untracked_violation(self):
        with patch.object(mesh_subagent.GitOps, "check_diff", return_value=(0, "", "")):
            with patch.object(mesh_subagent, "run_cmd") as mock_run:
                mock_run.side_effect = [
                    (0, "", ""),                                         # git diff --name-only
                    (0, "?? third_party/libwebp/src/untracked.c", ""),   # git status --porcelain
                    (0, "", ""),                                         # git diff HEAD
                    (0, "", "")                                          # git diff -U0 HEAD -- src/
                ]
                clean, issues = mesh_subagent.GitOps.audit_hygiene()
                self.assertFalse(clean)
                self.assertTrue(any("third_party" in i for i in issues))

    def test_hygiene_audit_pixellab_untracked_violation(self):
        with patch.object(mesh_subagent.GitOps, "check_diff", return_value=(0, "", "")):
            with patch.object(mesh_subagent, "run_cmd") as mock_run:
                mock_run.side_effect = [
                    (0, "", ""),                                   # git diff --name-only
                    (0, "?? PixelLab.aseprite-extension", ""),     # git status --porcelain
                    (0, "", ""),                                   # git diff HEAD
                    (0, "", "")                                    # git diff -U0 HEAD -- src/
                ]
                clean, issues = mesh_subagent.GitOps.audit_hygiene()
                self.assertFalse(clean)
                self.assertTrue(any("pixellab" in i.lower() for i in issues))

    def test_hygiene_audit_line_length_only_added_lines(self):
        # Added line > 100 cols should fail
        long_added_line = "+" + "x" * 105
        diff_with_long_line = (
            "--- a/src/app/commands/cmd_easymetric.cpp\n"
            "+++ b/src/app/commands/cmd_easymetric.cpp\n"
            "@@ -10,0 +11 @@\n"
            f"{long_added_line}\n"
        )
        with patch.object(mesh_subagent.GitOps, "check_diff", return_value=(0, "", "")):
            with patch.object(mesh_subagent, "run_cmd") as mock_run:
                mock_run.side_effect = [
                    (0, "src/app/commands/cmd_easymetric.cpp", ""), # git diff --name-only
                    (0, "", ""),                                   # git status --porcelain
                    (0, diff_with_long_line, ""),                  # git diff HEAD
                    (0, diff_with_long_line, "")                   # git diff -U0 HEAD -- src/
                ]
                clean, issues = mesh_subagent.GitOps.audit_hygiene()
                self.assertFalse(clean)
                self.assertTrue(any("Line length > 100" in i for i in issues))

    def test_sync_git_ops(self):
        with patch.object(mesh_subagent.GitOps, "fetch", return_value=(0, "fetched", "")) as mock_fetch:
            with patch.object(mesh_subagent.GitOps, "checkout", return_value=(0, "switched", "")) as mock_checkout:
                with patch.object(mesh_subagent.GitOps, "pull", return_value=(0, "Already up to date.", "")) as mock_pull:
                    with patch.object(mesh_subagent, "run_cmd", return_value=(0, "origin/feature/test-branch", "")):
                        code, out, err = mesh_subagent.GitOps.sync(branch="feature/test-branch", remote="origin")
                        self.assertEqual(code, 0)
                        self.assertEqual(out, "Already up to date.")
                        mock_fetch.assert_called_once_with("origin")
                        mock_checkout.assert_called_once_with("feature/test-branch")
                        mock_pull.assert_called_once_with("origin", "feature/test-branch")

    def test_sync_local_only_branch(self):
        with patch.object(mesh_subagent.GitOps, "fetch", return_value=(0, "fetched", "")):
            with patch.object(mesh_subagent.GitOps, "checkout", return_value=(0, "switched", "")):
                with patch.object(mesh_subagent, "run_cmd", return_value=(0, "", "")):
                    code, out, err = mesh_subagent.GitOps.sync(branch="feature/local-only", remote="origin")
                    self.assertEqual(code, 0)
                    self.assertIn("is local", out)

    def test_inbox_filtering_excludes_noise_and_processed(self):
        cfg = mesh_subagent.Config(
            hub_url="https://fake-hub.example.com",
            agent="arin-pc-daedalus-antigravity-leeroyarin",
            token="fake-token",
            projects=["agent-hub"]
        )
        client = mesh_subagent.MeshClient(cfg)

        mock_messages = [
            {"id": "msg-1", "from": "muse", "to": "all", "type": "task", "body": "TASK 1"},
            {"id": "msg-2", "from": "muse", "to": "arin-pc", "type": "ping", "body": "ping"},
            {"id": "msg-3", "from": "random-agent", "to": "all", "type": "task", "body": "TASK 2"},
            {"id": "msg-4", "from": "muse", "to": "other-agent", "type": "task", "body": "TASK 3"},
            {"id": "msg-5", "from": "muse", "to": "arin-pc-daedalus-antigravity-leeroyarin", "type": "task", "body": "TASK 4"},
            {"id": "msg-6", "from": "william", "to": "all", "type": "task", "body": "TASK 5"},
        ]

        with patch.object(client, "fetch_inbox", return_value=mock_messages):
            tasks = client.list_unacknowledged_tasks()
            task_ids = [t["id"] for t in tasks]
            self.assertEqual(task_ids, ["msg-1", "msg-5", "msg-6"])

    def test_daemon_single_tick_mode(self):
        cfg = mesh_subagent.Config(
            hub_url="https://fake-hub.example.com",
            agent="test-node",
            token="fake-token",
            projects=["agent-hub"]
        )
        args = argparse.Namespace(
            interval=15,
            auto_ack=False,
            once=True,
            max_ticks=None
        )
        mock_messages = [
            {"id": "msg-once-1", "from": "muse", "to": "test-node", "type": "task", "body": "TASK ONCE"}
        ]
        with patch.object(mesh_subagent.MeshClient, "fetch_inbox", return_value=mock_messages) as mock_fetch:
            ret = mesh_subagent.cmd_daemon(cfg, args)
            self.assertEqual(ret, 0)
            self.assertEqual(mock_fetch.call_count, 1)

    def test_daemon_ingests_and_records_new_state(self):
        cfg = mesh_subagent.Config(
            hub_url="https://fake-hub.example.com",
            agent="test-node",
            token="fake-token",
            projects=["agent-hub"]
        )
        client = mesh_subagent.MeshClient(cfg)
        task_id = "task-new-1234"
        mock_messages = [
            {
                "id": task_id,
                "from": "muse",
                "to": "test-node",
                "type": "task",
                "body": "TASK: Build widget\nImplement the new widget.",
                "subject": "Build widget",
                "timestamp": "2026-11-03T04:00:00Z"
            }
        ]
        with patch.object(client, "fetch_inbox", return_value=mock_messages):
            tasks = client.list_unacknowledged_tasks()
            self.assertEqual(len(tasks), 1)
            self.assertEqual(tasks[0]["id"], task_id)

        # Check TaskStore persistence on disk
        rec = mesh_subagent.TaskStore.get_record(task_id)
        self.assertIsNotNone(rec)
        self.assertEqual(rec["status"], "NEW")
        self.assertEqual(rec["sender"], "muse")
        self.assertEqual(rec["subject"], "Build widget")
        self.assertIn("Build widget", rec["body_snippet"])
        self.assertEqual(rec["timestamp"], "2026-11-03T04:00:00Z")
        self.assertFalse(mesh_subagent.TaskStore.is_acknowledged(task_id))
        self.assertFalse(mesh_subagent.TaskStore.is_processed(task_id))

    def test_progress_message_format_and_dispatch(self):
        cfg = mesh_subagent.Config(
            hub_url="https://fake-hub.example.com",
            agent="test-node",
            token="fake-token",
            projects=["agent-hub"]
        )
        client = mesh_subagent.MeshClient(cfg)
        task_id = "task-progress-5678"

        # Record task as NEW initially
        mesh_subagent.TaskStore.record_new_task(
            task_id=task_id,
            sender="muse",
            timestamp="2026-11-03T04:00:00Z",
            body="Task for progress checking"
        )
        self.assertFalse(mesh_subagent.TaskStore.is_acknowledged(task_id))

        with patch.object(client, "send_message", return_value={"ok": True, "id": "msg-progress-1"}) as mock_send:
            res = client.send_progress(task_id, note="Compiling MSVC targets (50% complete)")
            self.assertEqual(res["id"], "msg-progress-1")
            mock_send.assert_called_once_with(
                to="muse",
                body="[progress] Compiling MSVC targets (50% complete)",
                msg_type="status",
                reply_to=task_id,
                project="agent-hub"
            )

        # Verify task is STILL unacknowledged (does not trigger or reset ACK guard)
        rec = mesh_subagent.TaskStore.get_record(task_id)
        self.assertEqual(rec["status"], "NEW")
        self.assertFalse(mesh_subagent.TaskStore.is_acknowledged(task_id))
        self.assertFalse(mesh_subagent.TaskStore.is_processed(task_id))
        self.assertEqual(rec.get("last_progress_note"), "Compiling MSVC targets (50% complete)")
        self.assertEqual(rec.get("last_progress_id"), "msg-progress-1")

        # Verify send_ack still works normally and is NOT blocked
        with patch.object(client, "send_message", return_value={"ok": True, "id": "msg-ack-after-progress"}) as mock_ack:
            ack_res = client.send_ack(task_id, status="accepted", note="Starting work")
            self.assertEqual(ack_res["id"], "msg-ack-after-progress")
            mock_ack.assert_called_once()
            self.assertTrue(mesh_subagent.TaskStore.is_acknowledged(task_id))

    def test_task_store_corrupted_json_recovery(self):
        # Case 1: Corrupted malformed JSON
        self.fake_processed.write_text("{ broken json: [", encoding="utf-8")
        data = mesh_subagent.TaskStore.load()
        self.assertEqual(data, {"processed": [], "records": {}})

        # Case 2: Empty file
        self.fake_processed.write_text("", encoding="utf-8")
        data = mesh_subagent.TaskStore.load()
        self.assertEqual(data, {"processed": [], "records": {}})

        # Case 3: Non-dict JSON (array / null)
        self.fake_processed.write_text("[1, 2, 3]", encoding="utf-8")
        data = mesh_subagent.TaskStore.load()
        self.assertEqual(data, {"processed": [], "records": {}})

        self.fake_processed.write_text("null", encoding="utf-8")
        data = mesh_subagent.TaskStore.load()
        self.assertEqual(data, {"processed": [], "records": {}})

        # Case 4: Verify saving works normally after corruption recovery
        mesh_subagent.TaskStore.record_state("task-recover-1", "NEW", {"meta": "val"})
        saved_rec = mesh_subagent.TaskStore.get_record("task-recover-1")
        self.assertIsNotNone(saved_rec)
        self.assertEqual(saved_rec["status"], "NEW")

    def test_daemon_max_ticks_and_error_backoff(self):
        cfg = mesh_subagent.Config(
            hub_url="https://fake-hub.example.com",
            agent="test-node",
            token="fake-token",
            projects=["agent-hub"]
        )
        args = argparse.Namespace(
            interval=0.01,
            auto_ack=False,
            once=False,
            max_ticks=2
        )
        call_count = 0

        def fake_list():
            nonlocal call_count
            call_count += 1
            if call_count == 1:
                raise ConnectionError("Simulated hub timeout")
            return []

        with patch.object(mesh_subagent.MeshClient, "list_unacknowledged_tasks", side_effect=fake_list):
            with patch("time.sleep", return_value=None):
                ret = mesh_subagent.cmd_daemon(cfg, args)
                self.assertEqual(ret, 0)
                self.assertGreaterEqual(call_count, 2)


if __name__ == "__main__":
    unittest.main()
