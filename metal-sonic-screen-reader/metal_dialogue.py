"""Clipboard conversation and contextual Metal Sonic phrasing; no desktop I/O."""
from __future__ import annotations

from dataclasses import dataclass
import re

MAX_SPEECH = 600  # Matches fish_speech.py; every page must fit this limit.
MAX_COPY = 50_000
MAX_CONTEXT = 12_000
MAX_QUESTION = 4_000
MORE = " Copy Metal: continue for more."
QUOTES = (
    "SONIC DEAD OR ALIVE IS MINE.",
    "YOU CANNOT DO ANYTHING SO DON'T EVEN TRY.",
    "GET SOME HELP.",
    "DON'T DO WHAT SONIC DOES.",
)

# Deliberate, grammatical remixes. The meaning of the response selects the bank;
# random word shuffling would obscure the error or imply an action succeeded.
REMIXES = {
    "retry": ("YOU CAN TRY.", "YOU CAN TRY AGAIN.", "TRY THIS."),
    "error": ("DON'T DO WHAT SONIC DOES.", "YOU CAN TRY AGAIN.", "GET SOME HELP."),
    "blocked": ("YOU CANNOT DO THIS YET.", "DON'T EVEN TRY THE SAME STEP YET."),
    "success": ("THIS VICTORY IS MINE.", "THE BUILD IS ALIVE.", QUOTES[0]),
    "help": (QUOTES[2], "YOU CAN TRY."),
    "answer": ("TARGET ACQUIRED.", "YOU CAN TRY.", "LET'S DO THIS."),
}


@dataclass(frozen=True)
class Reply:
    kind: str
    message: str


@dataclass(frozen=True)
class Question:
    text: str
    context: str
    history: tuple[tuple[str, str], ...]


@dataclass(frozen=True)
class Outcome:
    speech: str = ""
    question: Question | None = None


def compact(text: str) -> str:
    return " ".join(text.split())


def pages(text: str) -> list[str]:
    """Keep words intact where possible, without dropping any source characters."""
    remaining = compact(text)
    result = []
    while len(remaining) > MAX_SPEECH:
        budget = MAX_SPEECH - len(MORE)
        cut = remaining.rfind(" ", 0, budget + 1)
        if cut < budget // 2:
            cut = budget  # Very long identifiers/URLs still need a bounded page.
        result.append(remaining[:cut] + MORE)
        remaining = remaining[cut:].lstrip()
    if remaining:
        result.append(remaining)
    return result


def context_excerpt(text: str) -> str:
    if len(text) <= MAX_CONTEXT:
        return text
    # Keep both the setup and the final exception of a long pasted log.
    marker = "\n[Middle of copied text omitted for length.]\n"
    head = MAX_CONTEXT * 2 // 3
    return text[:head] + marker + text[-(MAX_CONTEXT - head - len(marker)):]


def diagnose(text: str) -> Reply | None:
    """Conservative offline suggestions based on recognizable copied diagnostics."""
    missing = re.search(r"ModuleNotFoundError:\s*No module named ['\"]([^'\"\n]+)['\"]", text)
    if missing:
        return Reply("retry", f"Python could not import {missing[1]}. Check that the interpreter "
                     "and virtual environment are correct, then check the project's dependency list. "
                     "The installable package name may differ from the import name.")
    rules = (
        (r"(?:PermissionError:|\bEACCES\b|Permission denied)", "blocked",
         "Access was denied. Check the path, file ownership, and the permissions the operation needs before retrying."),
        (r"(?:FileNotFoundError:|\bENOENT\b|No such file or directory)", "retry",
         "A required path was not found. Check the working directory, spelling, and whether that file has been created."),
        (r"(?:SyntaxError:|IndentationError:|TabError:)", "error",
         "The parser rejected the code. Check the reported line and the line before it for missing punctuation, unmatched brackets, or indentation."),
        (r"(?:TypeError:)", "retry",
         "An operation received an incompatible value or argument. Check the types and function signature at the reported line."),
        (r"(?:NameError:|ReferenceError:)", "retry",
         "A name could not be resolved. Check its spelling, declaration, import, and scope at the reported line."),
        (r"(?:Cannot find module |ERR_MODULE_NOT_FOUND)", "retry",
         "A module could not be loaded. Check the import path and the dependency manifest, then use the project's package manager to restore dependencies."),
        (r"(?:\bEADDRINUSE\b|Address already in use)", "blocked",
         "The requested address is already in use. Identify which process owns the port, or choose another port before retrying."),
        (r"(?:\bECONNREFUSED\b|Connection refused)", "retry",
         "The connection was refused. Check that the service is running and that its host and port match the client settings."),
        (r"(?:\b429\s+Too Many Requests\b|rate limit exceeded)", "retry",
         "The service reported a rate limit. Respect its retry delay and reduce repeated requests before trying again."),
        (r"(?:TimeoutError:|\bETIMEDOUT\b|connection timed out)", "retry",
         "The operation timed out. Check service availability and connectivity before increasing the timeout."),
        (r"(?:^<<<<<<< |CONFLICT \(content\):)", "blocked",
         "The copied text contains a merge conflict. Compare both versions, resolve the conflict deliberately, and rerun the relevant checks."),
        (r"(?:AssertionError\b|^FAILED\s|^FAIL:|\b[1-9]\d* (?:tests? )?failed\b)", "error",
         "The copied output reports a failing check. Compare expected and actual values and isolate the smallest failing case before changing the code."),
        (r"(?:\bbuild succeeded\b|\bcompiled successfully\b|\b[1-9]\d* passed\b)", "success",
         "The copied output reports success. That confirms those checks; it does not establish that every behavior has been tested."),
    )
    for pattern, kind, message in rules:
        if re.search(pattern, text, re.I | re.M):
            return Reply(kind, message)
    return None


class Phrasebook:
    def __init__(self) -> None:
        self.indices: dict[str, int] = {}
        self.quote_index = 0

    def remix(self, kind: str) -> str:
        kind = kind if kind in REMIXES else "answer"
        choices = REMIXES[kind]
        index = self.indices.get(kind, 0)
        self.indices[kind] = index + 1
        return choices[index % len(choices)]

    def quote(self) -> str:
        quote = QUOTES[self.quote_index % len(QUOTES)]
        self.quote_index += 1
        return quote

    def say(self, reply: Reply) -> str:
        return self.remix(reply.kind) + " " + compact(reply.message)


class Conversation:
    """Session-only memory. Only explicit Metal: messages request a model reply."""

    def __init__(self, codex_available: bool = False) -> None:
        self.phrases = Phrasebook()
        self.codex_available = codex_available
        self.use_codex = codex_available
        self.muted = False
        self.source = ""
        self.history: list[tuple[str, str]] = []
        self.reading: list[str] = []
        self.page = 0

    def _read(self, text: str) -> Outcome:
        self.reading = pages(text)
        self.page = 0
        return Outcome(self.reading[0] if self.reading else "")

    def _notice(self, message: str) -> Outcome:
        # Controls must not replace the reading that repeat/continue refer to.
        return Outcome("" if self.muted else message)

    def _copy(self, text: str, literal: bool = False) -> Outcome:
        self.source = text[:MAX_COPY]
        spoken = self.source
        if len(text) > MAX_COPY:
            spoken += ". Copy limit reached; only the first 50,000 characters were retained."
        insight = None if literal else diagnose(self.source)
        if insight:
            spoken = self.phrases.remix(insight.kind) + " Copied output: " + spoken
        return self._read(spoken)

    def handle(self, value: str) -> Outcome:
        match = re.match(r"\A\s*metal(?: sonic)?\s*:\s*(.*)\Z", value, re.I | re.S)
        if not match:
            return Outcome() if self.muted else self._copy(value)
        command = match[1].strip()
        short = compact(command).lower().rstrip(".!?")
        if short in ("quiet", "stop", "silence"):
            self.muted = True
            return Outcome()
        if short in ("resume", "wake up"):
            self.muted = False
            return self._notice("I AM ALIVE. Clipboard speech resumed.")
        if short in ("forget", "reset"):
            self.source = ""
            self.history.clear()
            self.reading.clear()
            self.page = 0
            return self._notice("Conversation memory cleared. A NEW TARGET AWAITS.")
        if short in ("local", "codex"):
            self.use_codex = short == "codex" and self.codex_available
            if self.use_codex:
                return self._notice("GETTING SOME HELP. Codex replies enabled for Metal: questions and their copied context.")
            reason = "A compatible Codex CLI was not found. " if short == "codex" else ""
            return self._notice(reason + "Local replies enabled. I can recognize common errors and read your text.")
        if self.muted:
            return Outcome()
        if short in ("help", "commands", ""):
            return self._notice(
                "GET SOME HELP. Copy Metal: followed by your question to reply to me. "
                "Copy an error or code first, then Metal: explain. "
                "Use Metal: repeat, continue, quote, quiet, resume, or forget. "
                "Metal: local uses built-in replies. Metal: codex enables fuller replies. "
                "Metal: read followed by text reads it literally.")
        if short in ("quote", "catchphrase"):
            return self._notice(self.phrases.quote())
        if short in ("hello", "hi", "hey", "are you there"):
            return self._read(QUOTES[0] + " I am listening. Copy an error, then copy Metal: explain. Or copy Metal: followed by your message.")
        if short == "repeat":
            return self._notice(self.reading[self.page] if self.reading else "Copy something first. Then YOU CAN TRY again.")
        if short in ("continue", "next"):
            if self.page + 1 < len(self.reading):
                self.page += 1
                return Outcome(self.reading[self.page])
            return self._notice("Reading complete." if self.reading else "Nothing to continue. Copy some text first.")
        if short == "read":
            return self._read(self.source) if self.source else self._notice("Copy text first, or use Metal: read followed by the text.")
        literal = re.match(r"read\s+(.+)", command, re.I | re.S)
        if literal:
            return self._copy(literal[1], literal=True)
        if len(command) > MAX_QUESTION:
            return self._notice("GET SOME HELP. That message is too long. Copy the code or log separately, then copy a Metal: question under 4,000 characters.")
        question_text = re.sub(r"\A(?:ask|reply)\s+", "", command, flags=re.I)
        if short == "explain":
            if not self.source:
                return self._notice("GET SOME HELP. Copy the error or code first, then copy Metal: explain.")
            question_text = "Explain the copied text. If it contains a programming error, give the most useful next debugging step."
        question = Question(question_text, context_excerpt(self.source), tuple(self.history))
        if self.use_codex:
            return Outcome("GETTING SOME HELP. Analyzing your message.", question)
        return self.finish(question, self.local_reply(question))

    @staticmethod
    def local_reply(question: Question, problem: str = "") -> Reply:
        insight = diagnose(question.text) or diagnose(question.context)
        prefix = (problem + " ") if problem else "Local analysis. "
        if insight:
            return Reply(insight.kind, prefix + insight.message)
        return Reply("help", prefix + "Copy the exact error and relevant code, then copy Metal: explain. "
                     "Built-in replies cover common diagnostics; use Metal: codex for an open-ended conversation.")

    def finish(self, question: Question, reply: Reply) -> Outcome:
        self.history.extend((("user", question.text[:2000]), ("assistant", reply.message[:2000])))
        self.history = self.history[-6:]
        return self._read(self.phrases.say(reply))
