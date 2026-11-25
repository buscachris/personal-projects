#!/usr/bin/env python3
"""Read copied text and answer Metal: messages through the existing voice."""

from __future__ import annotations

import fcntl
from pathlib import Path
import signal
import sys
import time

import gi
import speechd

from metal_dialogue import Conversation, compact
from reply_engine import CodexReplies, ReplyWorker

gi.require_version("Gtk", "3.0")
gi.require_version("Gdk", "3.0")
from gi.repository import Gdk, GLib, Gtk  # noqa: E402

LOCK_FILE = Path.home() / ".cache" / "metal-sonic-screen-reader" / "clipboard.lock"


def main() -> int:
    LOCK_FILE.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    with LOCK_FILE.open("a+") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return 0  # A clipboard watcher is already running.

        if not Gdk.Display.get_default():
            print("No desktop display for clipboard reading", file=sys.stderr)
            return 1

        client = speechd.SSIPClient("MetalSonicClipboard")
        client.set_output_module("fish-metal")
        client.set_priority(speechd.Priority.MESSAGE)
        clipboard = Gtk.Clipboard.get(Gdk.SELECTION_CLIPBOARD)
        backend = CodexReplies()
        conversation = Conversation(codex_available=bool(backend.executable))
        state = {"timer": 0, "generation": 0, "last": "", "last_time": 0.0,
                 "requesting": False, "closed": False}

        def dispatch(callback) -> None:
            def deliver_when_settled() -> bool:
                if state["closed"]:
                    return False
                # Settle an incoming copy before delivering a model reply. A
                # clipboard manager's duplicate event should not erase a reply.
                if state["timer"] or state["requesting"]:
                    GLib.timeout_add(100, deliver_when_settled)
                    return False
                callback()
                return False
            GLib.idle_add(deliver_when_settled)

        worker = ReplyWorker(backend, dispatch)

        def speak(text: str) -> None:
            try:
                client.cancel()
                if text:
                    client.speak(text)
            except Exception as exc:
                # Do not log the clipboard contents or a model transcript.
                print(f"Clipboard speech failed ({type(exc).__name__})", file=sys.stderr)

        def received(_clipboard: Gtk.Clipboard, value: str | None, generation: int) -> None:
            if generation != state["generation"]:
                return
            state["requesting"] = False
            if not value:
                return
            phrase = compact(value)
            if not phrase:
                return
            now = time.monotonic()
            if value == state["last"] and now - state["last_time"] < 1.5:
                return  # Clipboard managers can announce one copy twice.
            state["last"], state["last_time"] = value, now
            worker.cancel()
            outcome = conversation.handle(value)
            speak(outcome.speech)
            if outcome.question:
                question = outcome.question

                def answered(reply) -> None:
                    speak(conversation.finish(question, reply).speech)

                worker.ask(question, answered)

        def request_latest() -> bool:
            state["timer"] = 0
            state["requesting"] = True
            clipboard.request_text(received, state["generation"])
            return False

        def changed(_clipboard: Gtk.Clipboard, _event: Gdk.EventOwnerChange) -> None:
            state["generation"] += 1
            if state["timer"]:
                GLib.source_remove(state["timer"])
            state["timer"] = GLib.timeout_add(350, request_latest)

        clipboard.connect("owner-change", changed)
        print("Metal Sonic dialogue active; copy Metal: help for controls; "
              f"Codex {'available' if backend.executable else 'not found'}", flush=True)

        def stop(_signum, _frame) -> None:
            GLib.idle_add(Gtk.main_quit)

        signal.signal(signal.SIGTERM, stop)
        signal.signal(signal.SIGINT, stop)
        try:
            Gtk.main()
        except KeyboardInterrupt:
            pass
        finally:
            state["closed"] = True
            worker.close()
            client.cancel()
            client.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
