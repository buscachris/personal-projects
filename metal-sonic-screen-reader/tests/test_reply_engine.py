import json
import os
from pathlib import Path
import queue
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

from metal_dialogue import Question, Reply
from reply_engine import CodexReplies, ReplyCancelled, ReplyUnavailable, ReplyWorker, find_codex, supports_dialogue


FAKE_CLI = r'''
import json, os, pathlib, sys, time
args = sys.argv[1:]
data = json.load(sys.stdin)
mode = data["message"]
if mode == "wait":
    pathlib.Path(data["copied_text"]).write_text(str(os.getpid()))
    time.sleep(20)
if mode == "auth":
    print("401 Authentication failed PRIVATE_ACCOUNT_DETAIL", file=sys.stderr)
    sys.exit(1)
destination = pathlib.Path(args[args.index("--output-last-message") + 1])
if mode == "malformed":
    destination.write_text('{"kind": [], "message": "bad"}')
elif mode == "blank":
    destination.write_text('{"kind": "answer", "message": " "}')
elif mode == "oversized":
    destination.write_text(json.dumps({"kind": "answer", "message": "x" * 4001}))
else:
    destination.write_text(json.dumps({"kind": "answer", "message": json.dumps(data)}))
'''


class BackendTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="metal-dialogue-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.cli = self.root / "fake-codex"
        self.cli.write_text(f"#!{sys.executable}\n" + FAKE_CLI)
        self.cli.chmod(0o700)
        self.backend = CodexReplies(str(self.cli), timeout=2)

    def test_user_text_stays_in_stdin_and_context_roundtrips(self):
        sentinel = self.root / "must-not-exist"
        text = f'$(touch {sentinel}) `touch {sentinel}`; Metal: why?'
        question = Question(text, "def f():\n    pass", (("user", "Earlier question"),))
        reply = self.backend.answer(question, threading.Event())
        payload = json.loads(reply.message)
        self.assertEqual(payload["message"], text)
        self.assertEqual(payload["copied_text"], question.context)
        self.assertEqual(payload["conversation"], [{"role": "user", "text": "Earlier question"}])
        self.assertFalse(sentinel.exists())
        args = self.backend.command(self.root)
        self.assertNotIn(text, args)
        self.assertIn("--ephemeral", args)
        self.assertIn("--ignore-user-config", args)
        self.assertEqual(args[args.index("--sandbox") + 1], "read-only")
        self.assertIn('web_search="disabled"', args)
        self.assertIn("shell_tool", args)
        self.assertIn("hooks", args)

    def test_discovery_skips_incompatible_desktop_cli(self):
        older = self.root / '.nvm/versions/node/v24.0/bin/codex'
        newer = self.root / '.nvm/versions/node/v22.0/bin/codex'
        for path in (older, newer):
            path.parent.mkdir(parents=True)
            path.write_text('#!/bin/sh\nexit 0\n')
            path.chmod(0o700)
        with patch.dict(os.environ, {}, clear=True), patch('reply_engine.Path.home', return_value=self.root), \
                patch('reply_engine.shutil.which', return_value=str(older)), \
                patch('reply_engine.supports_dialogue', side_effect=lambda path: path == newer) as probe:
            self.assertEqual(find_codex(), str(newer))
            self.assertEqual(probe.call_count, 2)

    def test_help_probe_requires_configuration_isolation(self):
        result = type('Result', (), {'returncode': 0, 'stdout': '--no-daemon --ephemeral --output-schema'})()
        with patch('reply_engine.subprocess.run', return_value=result):
            self.assertFalse(supports_dialogue(self.cli))
        result.stdout += ' --ignore-user-config'
        with patch('reply_engine.subprocess.run', return_value=result) as run:
            self.assertTrue(supports_dialogue(self.cli))
            self.assertTrue(all('--help' in call.args[0] for call in run.call_args_list))

    def test_auth_errors_do_not_expose_stderr(self):
        with self.assertRaises(ReplyUnavailable) as caught:
            self.backend.answer(Question("auth", "", ()), threading.Event())
        self.assertIn("sign-in", str(caught.exception))
        self.assertNotIn("PRIVATE_ACCOUNT", str(caught.exception))

    def test_malformed_empty_and_oversized_replies_are_rejected(self):
        for mode in ("malformed", "blank", "oversized"):
            with self.subTest(mode=mode), self.assertRaises(ReplyUnavailable):
                self.backend.answer(Question(mode, "", ()), threading.Event())

    def test_timeout_stops_child_process(self):
        self.backend.timeout = 0.3
        pid_file = self.root / "pid"
        started = time.monotonic()
        with self.assertRaisesRegex(ReplyUnavailable, "too long"):
            self.backend.answer(Question("wait", str(pid_file), ()), threading.Event())
        self.assertLess(time.monotonic() - started, 3)
        with self.assertRaises(ProcessLookupError):
            os.kill(int(pid_file.read_text()), 0)

    def test_cancel_stops_child_and_pre_cancel_does_not_start_one(self):
        cancel = threading.Event()
        timer = threading.Timer(0.3, cancel.set)
        self.addCleanup(timer.cancel)
        timer.start()
        pid_file = self.root / "pid"
        with self.assertRaises(ReplyCancelled):
            self.backend.answer(Question("wait", str(pid_file), ()), cancel)
        with self.assertRaises(ProcessLookupError):
            os.kill(int(pid_file.read_text()), 0)
        with patch("reply_engine.subprocess.Popen") as popen:
            with self.assertRaises(ReplyCancelled):
                self.backend.answer(Question("anything", "", ()), cancel)
            popen.assert_not_called()


class WorkerTests(unittest.TestCase):
    def make_worker(self, backend):
        dispatched = queue.Queue()
        worker = ReplyWorker(backend, dispatched.put)
        self.addCleanup(worker.close)
        return worker, dispatched

    def test_finished_reply_cannot_speak_after_new_copy(self):
        class Backend:
            def answer(self, question, cancelled):
                return Reply("answer", question.text)
        worker, dispatched = self.make_worker(Backend())
        spoken = []
        worker.ask(Question("old", "", ()), spoken.append)
        old_delivery = dispatched.get(timeout=2)
        worker.ask(Question("new", "", ()), spoken.append)
        new_delivery = dispatched.get(timeout=2)
        old_delivery()
        new_delivery()
        self.assertEqual(spoken, [Reply("answer", "new")])

    def test_active_work_is_cancelled_and_followup_can_complete(self):
        started, stopped = threading.Event(), threading.Event()
        class Backend:
            def answer(self, question, cancelled):
                if question.text == "old":
                    started.set()
                    if cancelled.wait(2):
                        stopped.set()
                    raise ReplyCancelled()
                return Reply("answer", "new answer")
        worker, dispatched = self.make_worker(Backend())
        spoken = []
        worker.ask(Question("old", "", ()), spoken.append)
        self.assertTrue(started.wait(2))
        worker.ask(Question("new", "", ()), spoken.append)
        self.assertTrue(stopped.wait(2))
        dispatched.get(timeout=2)()
        self.assertEqual(spoken, [Reply("answer", "new answer")])
        self.assertTrue(dispatched.empty())

    def test_service_failure_delivers_local_diagnosis(self):
        class Backend:
            def answer(self, question, cancelled):
                raise ReplyUnavailable("Codex took too long.")
        worker, dispatched = self.make_worker(Backend())
        spoken = []
        worker.ask(Question("Explain", "SyntaxError: invalid syntax", ()), spoken.append)
        dispatched.get(timeout=2)()
        self.assertEqual(spoken[0].kind, "error")
        self.assertIn("too long", spoken[0].message)
        self.assertIn("reported line", spoken[0].message)


if __name__ == "__main__":
    unittest.main()
