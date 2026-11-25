#!/usr/bin/env python3
"""Speech Dispatcher generic-module command for the Fish Audio Metal Sonic voice."""

from __future__ import annotations

import fcntl
from pathlib import Path
import shutil
import subprocess
import sys
import time

import requests

from metal_reader import ENDPOINT, VOICE_ID, api_key

MAX_CHARS = 600
LOCK_FILE = Path.home() / ".cache" / "metal-sonic-screen-reader" / "speech.lock"
# A light electronic modulation keeps the supplied voice recognizable while
# making navigation speech less human. Keep consonants clear for accessibility.
VOICE_FILTER = "highpass=f=100,lowpass=f=5200,tremolo=f=28:d=0.28,flanger=delay=2:depth=1:regen=0:width=35:speed=0.6"


def play_audio(audio: bytes) -> None:
    decoder = subprocess.run(
        ["ffmpeg", "-hide_banner", "-loglevel", "error", "-i", "pipe:0",
         "-af", VOICE_FILTER,
         "-f", "s16le", "-ac", "2", "-ar", "48000", "pipe:1"],
        input=audio, capture_output=True, check=True,
    )
    player = subprocess.Popen(
        ["paplay", "--volume=65536", "--client-name=Metal Sonic Screen Reader", "--raw", "--format=s16le", "--rate=48000", "--channels=2"],
        stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
    )
    _, error = player.communicate(decoder.stdout)
    if player.returncode:
        raise RuntimeError(f"Audio playback failed: {error.decode(errors='replace').strip()}")


def speak(message: str) -> None:
    # Speech Dispatcher cancels this process group when a new utterance
    # supersedes the old one. This small delay discards rapid cursor events
    # before spending a Fish API request.
    time.sleep(0.25)
    message = " ".join(message.split())[:MAX_CHARS]
    if not message:
        return

    LOCK_FILE.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    with LOCK_FILE.open("a+") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        try:
            response = requests.post(
                ENDPOINT,
                headers={
                    "Authorization": f"Bearer {api_key()}",
                    "Content-Type": "application/json",
                    "model": "s2.1-pro-free",
                },
                json={
                    "text": message,
                    "reference_id": VOICE_ID,
                    "format": "mp3",
                    "mp3_bitrate": 128,
                    "latency": "balanced",
                },
                timeout=(5, 35),
            )
            response.raise_for_status()
            if not response.content:
                raise RuntimeError("Fish Audio returned no audio")
            play_audio(response.content)
        except Exception as exc:
            if not shutil.which("espeak-ng"):
                raise
            print(f"Fish Audio unavailable; using eSpeak NG: {exc}", file=sys.stderr)
            fallback = subprocess.run(
                ["espeak-ng", "-v", "en-us", "-s", "180", "--stdout", message],
                capture_output=True, check=True,
            )
            play_audio(fallback.stdout)


if __name__ == "__main__":
    try:
        speak(" ".join(sys.argv[1:]))
    except Exception as exc:
        print(f"Metal Sonic speech error: {exc}", file=sys.stderr)
        raise SystemExit(1)
