# Metal Sonic screen reader

Orca reads desktop controls, text, and navigation through Speech Dispatcher.
The `fish-metal` output uses the existing Fish Audio Metal Sonic reference
voice, then adds a light electronic effect. A clipboard watcher reads newly
copied text through the same speech queue. Fast navigation cancels older
utterances, and key echo and progress bar chatter are disabled.

For Fish speech, supply a voice reference you can use via the `FISH_VOICE_ID` environment variable and configure your own Fish Audio API key. The Fish Speech Dispatcher module must inherit the environment setting. With Orca and the speech output configured, run `./start.sh` in this directory.

Running the launcher again has no effect while it is already active. Closing
Orca stops the clipboard watcher. The first new copy after startup is spoken;
existing clipboard contents are not read at startup. Password controls are
handled by Orca's normal accessibility rules.

## Talk back by copying text

Write a message anywhere, select it, and **copy it** (usually Ctrl+C):

```text
Metal: hello
Metal: Why does this function return None?
Metal: Can you explain that more simply?
```

To discuss an error or code, copy it first, then copy `Metal: explain`.
The next question remembers that copy and the last three question/answer pairs.
You can also copy a whole message such as `Metal: Explain this TypeError: ...`.
Copy the message itself, without Markdown fences. Pasting into an editor is a
convenient way to compose a reply; **copying the finished message sends it**.
There is no microphone requirement.

Codex supplies open-ended replies using the installed CLI and its existing
sign-in. The reader checks CLI compatibility and can find a newer installation
under NVM when the desktop PATH points at an older version. It runs a short,
read-only conversation with execution tools, hooks,
MCP integrations, apps, web search, and other-agent tools disabled. It does not
operate your project or dispatch tasks to OpenClaw/Antigravity. Ordinary copies
are read aloud; only a `Metal:` question requests a model reply. If Codex is
unavailable or takes over 45 seconds, common errors still get a built-in diagnosis.

| Copy this text | Result |
| --- | --- |
| `Metal: explain` | Explain the last copied code or error. |
| `Metal: repeat` | Repeat the current reading/reply page. |
| `Metal: continue` | Read the next page of a long copy or reply. |
| `Metal: quote` | Cycle through your four original catchphrases. |
| `Metal: quiet` / `Metal: resume` | Pause/resume clipboard speech and replies. Orca navigation continues. |
| `Metal: forget` | Clear the in-memory conversation and copied context. |
| `Metal: local` / `Metal: codex` | Choose built-in diagnostics or Codex replies for this session. |
| `Metal: read TEXT` | Read TEXT literally, including text beginning with `Metal:`. |
| `Metal: help` | Speak the controls. |

Metal Sonic's phrasing follows the meaning of the output, with rotating remixes:

- Missing dependency: **YOU CAN TRY.** Check the interpreter and project dependencies.
- Syntax error: **DON'T DO WHAT SONIC DOES.** Check the reported line and its preceding line.
- Permission failure: **YOU CANNOT DO THIS YET.** Check the path and required access.
- Passing checks: **THIS VICTORY IS MINE.** The copied output reports success.

Your original lines, including **SONIC DEAD OR ALIVE IS MINE**, are preserved in
`metal_dialogue.py`. Full quotes are available on demand; contextual remixes are
grammatical phrases selected for each response, not random word shuffling.
Copied error text is retained after its short character introduction. Routine
Orca focus/navigation announcements use their existing wording.

Readings are paginated to fit the voice's 600-character limit. Up to 50,000
characters of a copy are retained locally; model context uses at most 12,000
characters, preserving both ends of long logs. Questions are limited to 4,000
characters. A new copy cancels a pending reply; late replies are discarded.

The Fish Audio key stays in `~/.fish_audio_key`. Spoken screen content, copied
text, and replies are sent to Fish Audio for synthesis. Explicit `Metal:`
questions, their recent conversation, and copied context are also sent to
Codex when Codex replies are enabled. `Metal: local` stops those model requests.
Conversation memory lives only in this process; the CLI uses ephemeral sessions,
and this app does not log clipboard text or replies. `Metal: forget` clears
local memory; it does not erase anything already sent to a service.
If Fish Audio fails, the
speech output falls back to eSpeak NG. Audio runs through `ffmpeg` and
`paplay`. Reader/dialogue errors are logged in
`~/.cache/metal-sonic-screen-reader/screen-reader.log`. Speech synthesis errors
are in `$XDG_RUNTIME_DIR/speech-dispatcher/log/fish-metal.log`.

Run offline checks with `python3 -m unittest discover -s tests -v`.
`METAL_SONIC_CODEX=/absolute/path/to/codex` optionally selects another installed
CLI. It must support `--no-daemon`, `exec --ignore-user-config`, `--ephemeral`,
and `--output-schema`; otherwise built-in replies remain available. No extra
API key is needed. CLI integration follows the installed help and
the [Codex configuration reference](https://learn.chatgpt.com/docs/config-file/config-reference).
