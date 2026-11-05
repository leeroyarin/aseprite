import io
import json
from pathlib import Path
import random
import sys
import tempfile
import unittest
from unittest.mock import patch
from contextlib import redirect_stderr
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "sidecar"))
import agent_sidecar as client
from helpers import check_no_cli_token, get_env_token, compute_backoff_delay

class FakeClient:
    def __init__(self, data): self.data=data; self.requests=[]
    def open(self, request, timeout): self.requests.append(request); return io.BytesIO(json.dumps(self.data).encode())

class OutboundClientTests(unittest.TestCase):
    def test_environment_auth_and_cli_guard(self):
        self.assertEqual(get_env_token({"AGENT_HUB_TOKEN":"local-fixture"}),"local-fixture")
        with self.assertRaises(ValueError): get_env_token({})
        with self.assertRaises(ValueError): check_no_cli_token(["--token=local-fixture"])
    def test_only_existing_https_origin(self):
        self.assertEqual(client.validate_url(client.HUB_URL),client.HUB_URL)
        for url in ["http://127.0.0.1:8787", "https://other.invalid",client.HUB_URL+"?token=value"]:
            with self.assertRaises(ValueError): client.validate_url(url)
    def test_bearer_header_and_since_zero_paging(self):
        fake=FakeClient({"messages":[]})
        client.fetch_page("local-fixture", "alpha", "0", fake)
        req=fake.requests[0]; self.assertEqual(req.get_header("Authorization"),"Bearer local-fixture")
        self.assertIn("since=0",req.full_url);self.assertNotIn("local-fixture",req.full_url)
    def test_append_only_dedup_and_literal_command_data(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/"inbox.jsonl";page={"messages":[{"id":"m1","type":"command","body":"echo harmless literal"}]}
            self.assertEqual(client.save_page(path,page,"local-fixture"),1)
            before=path.read_bytes();self.assertEqual(client.save_page(path,page,"local-fixture"),0)
            self.assertEqual(path.read_bytes(),before);self.assertIn("echo harmless literal",path.read_text())
    def test_redacts_before_saving(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/"inbox.jsonl"
            client.save_page(path,{"messages":[{"id":"m1","body":"password=private-value local-fixture"}]},"local-fixture")
            text=path.read_text();self.assertNotIn("private-value",text);self.assertNotIn("local-fixture",text)
    def test_persisted_cursor_advances_only_after_save(self):
        with tempfile.TemporaryDirectory() as folder:
            inbox=Path(folder)/"inbox.jsonl";cursor=Path(folder)/"cursor.json"
            fake=FakeClient({"messages":[{"id":"m1","body":"data"}],"latest_id":"m1","has_more":False})
            self.assertEqual(client.poll_once("alpha",inbox,cursor,"fixture",fake),(1,False))
            self.assertEqual(json.loads(cursor.read_text())["cursor"],"m1")
            self.assertEqual(client.poll_once("alpha",inbox,cursor,"fixture",fake),(0,False))
            self.assertIn("since=m1",fake.requests[-1].full_url)
    def test_refuses_redirects_and_invalid_shape(self):
        with self.assertRaises(ValueError): client.NoRedirect().redirect_request(None,None,302,"",{},"https://other.invalid")
        with self.assertRaises(ValueError): client.fetch_page("fixture","alpha","0",FakeClient({"messages":"wrong"}))
    def test_usage_report_only_posts_explicit_data_to_hub(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/"usage.json";path.write_text(json.dumps({"id":"usage-fixture","source":"tool","budget_remaining_percent":10}))
            fake=FakeClient({"id":"usage-fixture"});client.post_report("local-fixture","alpha",path,"usage",opener=fake)
            request=fake.requests[0];self.assertEqual(request.method,"POST");self.assertTrue(request.full_url.endswith("/api/v2/projects/alpha/usage"));self.assertEqual(request.get_header("Authorization"),"Bearer local-fixture")
            self.assertEqual(json.loads(request.data)["budget_remaining_percent"],10)
    def test_handoff_report_redacts_and_does_not_execute(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/"handoff.json";path.write_text(json.dumps({"id":"handoff-fixture","summary":"password=sensitive local-fixture","tests":[{"command":"literal command data"}]}))
            fake=FakeClient({"id":"handoff-fixture"});client.post_report("local-fixture","alpha",path,"handoff",task="task/one",opener=fake)
            request=fake.requests[0];self.assertIn("task%2Fone/handoffs",request.full_url);self.assertNotIn(b"sensitive",request.data);self.assertNotIn(b"local-fixture",request.data);self.assertIn(b"literal command data",request.data)
    def test_missing_or_oversize_report_is_not_fabricated(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/"absent.json"
            with self.assertRaises(OSError):client.post_report("fixture","alpha",path,"usage",opener=FakeClient({}))
            path.write_text("x"*65537)
            with self.assertRaises(ValueError):client.post_report("fixture","alpha",path,"usage",opener=FakeClient({}))
    def test_report_kind_cannot_select_an_arbitrary_endpoint(self):
        with self.assertRaises(ValueError):client.post_report("fixture","alpha","absent","../tokens",opener=FakeClient({}))
    def test_bad_evidence_report_does_not_prevent_inbox_poll(self):
        output=io.StringIO()
        with patch.object(client,"poll_once",return_value=(0,False)) as poll,patch.object(client,"post_report",side_effect=ValueError("local-fixture-private")),patch.object(client,"get_env_token",return_value="local-fixture"),patch.dict(client.os.environ,{"HUB_URL":client.HUB_URL}),redirect_stderr(output):
            result=client.main(["--project","alpha","--inbox","fixture-inbox.jsonl","--cursor","fixture-cursor.json","--usage-report","fixture-usage.json","--once"])
        self.assertEqual(result,1);poll.assert_called_once();self.assertNotIn("local-fixture-private",output.getvalue())
    def test_redacts_secret_json_keys_and_other_mesh_token_shapes(self):
        from helpers import redact_payload
        value=redact_payload({"local-fixture":"amt_"+"a"*32},extra_secret="local-fixture")
        rendered=json.dumps(value);self.assertNotIn("local-fixture",rendered);self.assertNotIn("amt_",rendered)
    def test_reused_backoff_is_bounded(self):
        self.assertEqual(compute_backoff_delay(0,jitter_ratio=0),1)
        self.assertLessEqual(compute_backoff_delay(8,rng=random.Random(1)),60)

if __name__ == "__main__": unittest.main()
