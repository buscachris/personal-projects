#!/usr/bin/env python3
"""Read desktop accessibility events with the existing Fish Audio Metal Sonic voice."""

from __future__ import annotations

import argparse
from collections import OrderedDict
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import threading
import time

import requests

VOICE_ID = os.environ.get("FISH_VOICE_ID", "")
KEY_FILE = Path.home() / ".fish_audio_key"
ENDPOINT = "https://api.fish.audio/v1/tts"


def api_key() -> str:
    """Read the existing key file as data; never execute its shell contents."""
    try:
        data = KEY_FILE.read_text()
    except OSError as exc:
        raise RuntimeError(f"Cannot read {KEY_FILE}: {exc}") from exc
    match = re.search(r"^\s*(?:export\s+)?FISH_API_KEY=['\"]?([^'\"\s#]+)", data, re.M)
    if not match:
        raise RuntimeError(f"FISH_API_KEY was not found in {KEY_FILE}")
    return match.group(1)


def clean(text: str) -> str:
    return " ".join(text.split())[:450]


class Speaker:
    def __init__(self, key: str, voice_id: str, *, fallback: bool = True):
        self.key = key
        self.voice_id = voice_id
        self.fallback = fallback
        self.cache: OrderedDict[str, bytes] = OrderedDict()
        self.lock = threading.Lock()
        self.work_lock = threading.Lock()
        self.generation = 0
        self.player: subprocess.Popen | None = None

    def stop(self) -> None:
        with self.lock:
            self.generation += 1
            if self.player and self.player.poll() is None:
                self.player.terminate()
            self.player = None

    def say(self, text: str) -> None:
        text = clean(text)
        if not text:
            return
        self.stop()
        with self.lock:
            token = self.generation
        threading.Thread(target=self._speak, args=(text, token), daemon=True).start()

    def _current(self, token: int) -> bool:
        with self.lock:
            return token == self.generation

    def _speak(self, text: str, token: int) -> None:
        with self.work_lock:
            if self._current(token):
                self._speak_locked(text, token)

    def _speak_locked(self, text: str, token: int) -> None:
        try:
            audio = self.cache.get(text)
            if audio is None:
                response = requests.post(
                    ENDPOINT,
                    headers={
                        "Authorization": f"Bearer {self.key}",
                        "Content-Type": "application/json",
                        "model": "s2.1-pro-free",
                    },
                    json={
                        "text": text,
                        "reference_id": self.voice_id,
                        "format": "mp3",
                        "mp3_bitrate": 128,
                        "latency": "balanced",
                    },
                    timeout=(5, 35),
                )
                response.raise_for_status()
                audio = response.content
                if not audio:
                    raise RuntimeError("Fish Audio returned empty audio")
                if self._current(token):
                    self.cache[text] = audio
                    self.cache.move_to_end(text)
                    if len(self.cache) > 64:
                        self.cache.popitem(last=False)
            if not self._current(token):
                return
            decoded = subprocess.run(
                ["ffmpeg", "-hide_banner", "-loglevel", "error", "-i", "pipe:0",
                 "-f", "s16le", "-ac", "2", "-ar", "48000", "pipe:1"],
                input=audio, capture_output=True,
            )
            if decoded.returncode != 0 or not decoded.stdout:
                raise RuntimeError(f"MP3 decoding failed: {decoded.stderr.decode(errors='replace').strip()}")
            if not self._current(token):
                return
            player = subprocess.Popen(
                ["paplay", "--volume=65536", "--client-name=Metal Sonic Screen Reader", "--raw", "--format=s16le", "--rate=48000", "--channels=2"],
                stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
            )
            with self.lock:
                if token != self.generation:
                    player.terminate()
                    return
                self.player = player
            try:
                _, error = player.communicate(decoded.stdout)
                if player.returncode and self._current(token):
                    raise RuntimeError(f"PulseAudio playback failed: {error.decode(errors='replace').strip()}")
            except BrokenPipeError:
                pass
            finally:
                with self.lock:
                    if self.player is player:
                        self.player = None
        except Exception as exc:
            if self._current(token):
                print(f"Fish Audio speech failed: {exc}", file=sys.stderr)
                if self.fallback and shutil.which("espeak-ng"):
                    player = subprocess.Popen(["espeak-ng", text], stdout=subprocess.DEVNULL)
                    with self.lock:
                        if token != self.generation:
                            player.terminate()
                        else:
                            self.player = player
                    player.wait()
                    with self.lock:
                        if self.player is player:
                            self.player = None


def accessible_text(obj, *, line: bool = False) -> str:
    """Describe one accessible object without reading password fields."""
    from gi.repository import Atspi

    try:
        role = obj.get_role()
        if role == Atspi.Role.PASSWORD_TEXT:
            return "Password field"
        name = clean(obj.get_name() or "")
        role_name = clean(obj.get_role_name() or "")
        value = ""
        text_iface = obj.get_text_iface()
        if text_iface:
            count = text_iface.get_character_count()
            if count > 0:
                if line:
                    caret = max(0, text_iface.get_caret_offset())
                    line_range = text_iface.get_string_at_offset(
                        min(caret, count - 1), Atspi.TextGranularity.LINE
                    )
                    value = clean(line_range.content or "") if line_range else ""
                elif not name:
                    text_range = text_iface.get_string_at_offset(0, Atspi.TextGranularity.PARAGRAPH)
                    value = clean(text_range.content or "") if text_range else ""
        if line:
            return value or name
        parts = [part for part in (name, value, role_name) if part]
        return ", ".join(parts)
    except Exception:
        return ""


def follow(speaker: Speaker) -> None:
    import gi

    gi.require_version("Atspi", "2.0")
    from gi.repository import Atspi, GLib

    if Atspi.init() != 0:
        raise RuntimeError("Could not connect to the desktop accessibility bus")

    pending: dict[str, object] = {"timer": 0, "text": "", "last": "", "at": 0.0}

    def flush() -> bool:
        pending["timer"] = 0
        phrase = pending["text"]
        if phrase and (phrase != pending["last"] or time.monotonic() - pending["at"] > 3):
            pending["last"] = phrase
            pending["at"] = time.monotonic()
            speaker.say(phrase)
        return False

    def enqueue(phrase: str) -> None:
        phrase = clean(phrase)
        if not phrase:
            return
        if pending["timer"]:
            GLib.source_remove(pending["timer"])
        pending["text"] = phrase
        pending["timer"] = GLib.timeout_add(230, flush)

    def on_focus(event, _user_data=None) -> None:
        try:
            if not event.detail1:
                return
            enqueue(accessible_text(event.source))
        except Exception as exc:
            print(f"Accessibility event error: {exc}", file=sys.stderr)

    def on_caret(event, _user_data=None) -> None:
        try:
            enqueue(accessible_text(event.source, line=True))
        except Exception:
            pass

    listeners = [
        (Atspi.EventListener.new(on_focus), "object:state-changed:focused"),
        (Atspi.EventListener.new(on_caret), "object:text-caret-moved"),
    ]
    for listener, event_type in listeners:
        if not listener.register(event_type):
            raise RuntimeError(f"Could not register {event_type}")
    print("Metal Sonic reader active. Press Ctrl+C to stop.", flush=True)
    try:
        GLib.MainLoop().run()
    except KeyboardInterrupt:
        pass
    finally:
        for listener, event_type in listeners:
            listener.deregister(event_type)
        speaker.stop()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--say", metavar="TEXT", help="Speak text once to test the voice")
    parser.add_argument("--voice-id", default=VOICE_ID, help="Fish Audio voice reference ID")
    parser.add_argument("--no-fallback", action="store_true", help="Disable eSpeak fallback")
    args = parser.parse_args()
    for program in ("ffmpeg", "paplay"):
        if not shutil.which(program):
            parser.error(f"{program} is needed for audio playback")
    try:
        speaker = Speaker(api_key(), args.voice_id, fallback=not args.no_fallback)
        if args.say is not None:
            speaker._speak(args.say, speaker.generation)
        else:
            follow(speaker)
    except (OSError, RuntimeError) as exc:
        print(exc, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
