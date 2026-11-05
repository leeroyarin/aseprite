import ast
from datetime import datetime, timedelta, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT=Path(__file__).resolve().parents[1]/"sidecar/hub_command_log.py"
spec=importlib.util.spec_from_file_location("command_logger",SCRIPT)
logger=importlib.util.module_from_spec(spec);spec.loader.exec_module(logger)

class CommandLoggerTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.chat=self.root/"tool"/"sessions";self.chat.mkdir(parents=True)
        self.chatfile=self.chat/"session.jsonl";self.original=json.dumps({"type":"session_meta","payload":{"originator":"fixture"}})+"\n"+json.dumps({"type":"normal_chat","body":"private chat stays here"})+"\n"
        self.chatfile.write_text(self.original,encoding="utf-8")
        self.evidence=logger.discover_codex(self.chat.parent);self.evidence_path=self.root/"discovery.json";self.evidence_path.write_text(json.dumps(self.evidence))
        self.commands=self.root/"commands"
        self.config=logger.install(self.chat,self.commands,self.evidence_path)
    def packet(self,**changes):
        return {"id":"command-one","project":"alpha","from":"hub-supervisor","type":"command","body":"literal work request","reply_to":None,**changes}
    def test_discovers_metadata_and_never_copies_chat_text(self):
        self.assertEqual(Path(self.evidence["chat_path"]),self.chat.resolve());self.assertEqual(self.chatfile.read_text(),self.original)
        self.assertNotIn("private chat",json.dumps(self.evidence));self.assertNotIn("private chat",json.dumps(self.config))
    def test_refuses_unverified_or_guessed_chat_storage(self):
        with self.assertRaises(ValueError):logger.discover_codex(self.root/"commands")
        bad=self.root/"bad.json";bad.write_text(json.dumps({"chat_path":str(self.chat),"tool":"fixture","method":"guess","evidence_paths":[str(self.root/"missing")]}))
        with self.assertRaises(ValueError):logger.install(self.chat,self.root/"other",bad)
    def test_requires_separate_chat_and_command_trees(self):
        for target in [self.chat,self.chat/"commands",self.chat.parent]:
            with self.assertRaises(ValueError):logger.install(self.chat,target,self.evidence_path)
    def test_installs_exact_script_and_preserves_existing_manifest(self):
        script=self.commands/"hub_command_log.py";self.assertEqual(script.read_bytes(),SCRIPT.read_bytes());self.assertEqual(self.config["script_sha256"],hashlib.sha256(script.read_bytes()).hexdigest())
        old=(self.commands/"manifest.json").read_bytes();logger.install(self.chat,self.commands,self.evidence_path);self.assertEqual((self.commands/"manifest.json").read_bytes(),old)
    def test_does_not_overwrite_another_installed_script(self):
        target=self.root/"other";target.mkdir();script=target/"hub_command_log.py";script.write_text("different script")
        with self.assertRaises(ValueError):logger.install(self.chat,target,self.evidence_path)
        self.assertEqual(script.read_text(),"different script")
    def test_normal_messages_are_ignored_without_a_command_record(self):
        self.assertEqual(logger.log_command(self.config,self.packet(type="message")),{"ignored":True});self.assertFalse((self.commands/"commands.jsonl").exists());self.assertEqual(self.chatfile.read_text(),self.original)
    def test_duplicate_message_received_is_skipped(self):
        self.assertFalse(logger.log_command(self.config,self.packet())["duplicate"]);before=(self.commands/"commands.jsonl").read_bytes();self.assertTrue(logger.log_command(self.config,self.packet())["duplicate"]);self.assertEqual((self.commands/"commands.jsonl").read_bytes(),before)
    def test_status_changes_append_events_without_editing_old_lines(self):
        logger.log_command(self.config,self.packet());first=(self.commands/"commands.jsonl").read_bytes()
        logger.log_command(self.config,self.packet(),"started");logger.log_command(self.config,self.packet(),"done")
        data=(self.commands/"commands.jsonl").read_bytes();self.assertTrue(data.startswith(first));self.assertEqual([json.loads(x)["status"] for x in data.splitlines()],["received","started","done"])
        with self.assertRaises(ValueError):logger.log_command(self.config,self.packet(),"failed")
    def test_rejects_conflicting_ids_and_skipped_status_transitions(self):
        with self.assertRaises(ValueError):logger.log_command(self.config,self.packet(),"started")
        logger.log_command(self.config,self.packet());before=(self.commands/"commands.jsonl").read_bytes()
        with self.assertRaises(ValueError):logger.log_command(self.config,self.packet(body="different"))
        with self.assertRaises(ValueError):logger.log_command(self.config,self.packet(),"done")
        self.assertEqual((self.commands/"commands.jsonl").read_bytes(),before)
    def test_redacts_secret_shaped_text_and_keeps_commands_literal(self):
        marker=self.root/"must-not-exist"
        text="password=private-value amt_"+"a"*32+" __import__('pathlib').Path("+repr(str(marker))+").touch()"
        logger.log_command(self.config,self.packet(body=text));data=(self.commands/"commands.jsonl").read_text();self.assertNotIn("private-value",data);self.assertNotIn("amt_",data);self.assertIn("__import__",data);self.assertFalse(marker.exists())
    def test_refuses_credential_shaped_ids_and_oversize_command_text(self):
        with self.assertRaises(ValueError):logger.log_command(self.config,self.packet(id="amt_"+"a"*32))
        with self.assertRaises(ValueError):logger.log_command(self.config,self.packet(body="é"*17000))
        with self.assertRaises(ValueError):logger.log_command(self.config,self.packet(reply_to={"normal_chat":"must not be recorded"}))
    def test_never_silently_repairs_corrupt_or_locked_history(self):
        path=self.commands/"commands.jsonl";path.write_bytes(b"corrupt history\n")
        with self.assertRaises(ValueError):logger.log_command(self.config,self.packet())
        self.assertEqual(path.read_bytes(),b"corrupt history\n")
        lock=self.commands/"command-log.lock";lock.write_text("other process")
        with self.assertRaises(OSError):logger.log_command(self.config,self.packet())
        self.assertEqual(lock.read_text(),"other process")
    def test_proof_requires_the_installed_entrypoint(self):
        with self.assertRaises(ValueError):logger.proof(self.config,self.packet())
    def test_installed_proof_logs_once_and_refuses_expired_challenge(self):
        packet=self.packet(type="enrollment.challenge",body=json.dumps({"challenge_id":"fixture-challenge","code":"c"*64,"script_sha256":self.config["script_sha256"],"rulebook_sha256":"d"*64,"expires_at":(datetime.now(timezone.utc)+timedelta(minutes=5)).isoformat()}))
        file=self.root/"challenge.json";file.write_text(json.dumps(packet));args=[sys.executable,str(self.commands/"hub_command_log.py"),"proof","--config",str(self.commands/"manifest.json"),"--message-file",str(file)]
        first=subprocess.run(args,capture_output=True,text=True,check=True);second=subprocess.run(args,capture_output=True,text=True,check=True);self.assertEqual(json.loads(first.stdout),json.loads(second.stdout));self.assertEqual(len((self.commands/"commands.jsonl").read_text().splitlines()),1)
        body=json.loads(packet["body"]);body["expires_at"]="2000-01-01T00:00:00+00:00";packet["body"]=json.dumps(body);file.write_text(json.dumps(packet));expired=subprocess.run(args,capture_output=True,text=True);self.assertEqual(expired.returncode,2);self.assertEqual(len((self.commands/"commands.jsonl").read_text().splitlines()),1)
    def test_cli_errors_never_echo_credentials_or_parse_tool_chats(self):
        result=subprocess.run([sys.executable,str(SCRIPT),"--token=fixture-private-value"],capture_output=True,text=True);self.assertEqual(result.returncode,2);self.assertNotIn("fixture-private-value",result.stdout+result.stderr)
        result=subprocess.run([sys.executable,str(self.commands/"hub_command_log.py"),"log","--config",str(self.commands/"manifest.json"),"--message-file",str(self.chatfile)],capture_output=True,text=True);self.assertEqual(result.returncode,2);self.assertEqual(self.chatfile.read_text(),self.original)
    def test_standalone_script_has_no_network_or_execution_entrypoints(self):
        tree=ast.parse(SCRIPT.read_text())
        for node in ast.walk(tree):
            if isinstance(node,(ast.Import,ast.ImportFrom)):
                names=[x.name for x in node.names] if isinstance(node,ast.Import) else [node.module or ""]
                self.assertFalse(any(x.split('.')[0] in {'socket','urllib','requests','subprocess','http','websocket'} for x in names))
            if isinstance(node,ast.Call):
                name=node.func.id if isinstance(node.func,ast.Name) else node.func.attr if isinstance(node.func,ast.Attribute) else ''
                self.assertNotIn(name,{'eval','exec','system','popen','Popen','run','spawn'})

if __name__=="__main__":unittest.main()
