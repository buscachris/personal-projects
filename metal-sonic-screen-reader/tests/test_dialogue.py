import unittest

from metal_dialogue import (
    MAX_CONTEXT, MAX_COPY, MAX_QUESTION, MAX_SPEECH, MORE, QUOTES,
    Conversation, Phrasebook, Question, Reply, compact, context_excerpt,
    diagnose, pages,
)


class DialogueTests(unittest.TestCase):
    def test_original_quotes_and_meaningful_remixes(self):
        bank = Phrasebook()
        self.assertEqual([bank.quote() for _ in range(5)], [*QUOTES, QUOTES[0]])
        self.assertEqual(bank.say(Reply("retry", "Check the import.")),
                         "YOU CAN TRY. Check the import.")
        self.assertNotEqual(bank.remix("retry"), "YOU CAN TRY.")
        self.assertIn("CANNOT", bank.remix("blocked"))

    def test_diagnostics_choose_failure_before_passing_tests(self):
        cases = (
            ("ModuleNotFoundError: No module named 'cv2'", "retry", "may differ"),
            ("PermissionError: [Errno 13] Permission denied", "blocked", "ownership"),
            ("SyntaxError: invalid syntax", "error", "line before"),
            ("TypeError: incompatible arguments", "retry", "types"),
            ("Error: listen EADDRINUSE", "blocked", "port"),
            ("3 passed, 1 failed", "error", "failing"),
            ("3 passed, 0 failed", "success", "copied output"),
        )
        for text, kind, detail in cases:
            with self.subTest(text=text):
                reply = diagnose(text)
                self.assertEqual(reply.kind, kind)
                self.assertIn(detail, reply.message)
        self.assertIsNone(diagnose("a function named failure_count"))
        self.assertIsNone(diagnose("0 failed"))

    def test_plain_copy_is_read_without_model_and_keeps_indentation(self):
        convo = Conversation(codex_available=True)
        code = 'def thing():\n    return "Metal: explain"\n'
        outcome = convo.handle(code)
        self.assertIsNone(outcome.question)
        self.assertEqual(outcome.speech, compact(code))
        self.assertEqual(convo.source, code)
        outcome = convo.handle("Metal: explain")
        self.assertEqual(outcome.question.context, code)

    def test_error_copy_keeps_original_diagnostic(self):
        convo = Conversation(codex_available=True)
        original = "ModuleNotFoundError: No module named 'cv2'"
        result = convo.handle(original)
        self.assertTrue(result.speech.startswith("YOU CAN TRY."))
        self.assertIn(original, result.speech)
        self.assertIsNone(result.question)
        result = convo.handle("Metal: local")
        self.assertIsNone(result.question)
        result = convo.handle("Metal: explain")
        self.assertIn("interpreter", result.speech)
        self.assertIsNone(result.question)

    def test_literal_read_escapes_commands(self):
        convo = Conversation(codex_available=True)
        result = convo.handle("Metal: read Metal: quiet")
        self.assertEqual(result.speech, "Metal: quiet")
        self.assertFalse(convo.muted)
        self.assertEqual(convo.source, "Metal: quiet")
        self.assertIsNone(result.question)
        self.assertEqual(convo.handle("Metal: read SyntaxError: example").speech,
                         "SyntaxError: example")

    def test_pagination_preserves_words_and_allows_repeat_and_continue(self):
        text = " ".join(f"token{n}" for n in range(650))
        result = pages(text)
        self.assertTrue(all(len(page) <= MAX_SPEECH for page in result))
        restored = " ".join(page.removesuffix(MORE) for page in result)
        self.assertEqual(restored, text)
        self.assertEqual(pages("x" * 600), ["x" * 600])
        result = pages("x" * 1600)
        self.assertEqual("".join(page.removesuffix(MORE) for page in result), "x" * 1600)
        convo = Conversation()
        first = convo.handle(text).speech
        convo.handle("Metal: help")
        convo.handle("Metal: quote")
        self.assertEqual(convo.handle("Metal: repeat").speech, first)
        second = convo.handle("Metal: continue").speech
        self.assertNotEqual(second, first)
        self.assertEqual(convo.handle("Metal: repeat").speech, second)
        for _ in range(len(convo.reading) - 2):
            convo.handle("Metal: continue")
        self.assertEqual(convo.handle("Metal: continue").speech, "Reading complete.")

    def test_context_is_bounded_and_preserves_exception_at_end(self):
        text = "HEADER\n" + "x" * 20000 + "\nTypeError: END"
        snippet = context_excerpt(text)
        self.assertEqual(len(snippet), MAX_CONTEXT)
        self.assertTrue(snippet.startswith("HEADER\n"))
        self.assertTrue(snippet.endswith("TypeError: END"))
        convo = Conversation(codex_available=True)
        convo.handle("x" * (MAX_COPY + 1000))
        self.assertEqual(len(convo.source), MAX_COPY)
        self.assertIn("Copy limit reached", convo.reading[-1])
        self.assertIsNone(convo.handle("Metal: " + "?" * (MAX_QUESTION + 1)).question)

    def test_followups_keep_recent_history_and_forget_clears_it(self):
        convo = Conversation(codex_available=True)
        convo.handle("TypeError: example")
        for n in range(4):
            q = convo.handle(f"Metal: question {n}").question
            convo.finish(q, Reply("answer", f"answer {n}"))
        followup = convo.handle("metal sonic: explain that more simply").question
        self.assertEqual(len(followup.history), 6)
        self.assertEqual(followup.history[0], ("user", "question 1"))
        self.assertEqual(followup.context, "TypeError: example")
        convo.handle("Metal: forget")
        self.assertEqual(convo.history, [])
        self.assertEqual(convo.source, "")
        self.assertEqual(convo.reading, [])
        self.assertIsNone(convo.handle("Metal: explain").question)

    def test_quiet_resumes_without_reading_intervening_copies(self):
        convo = Conversation(codex_available=True)
        convo.handle("old context")
        self.assertEqual(convo.handle("Metal: quiet").speech, "")
        self.assertEqual(convo.handle("new context").speech, "")
        self.assertIsNone(convo.handle("Metal: do you hear me?").question)
        self.assertEqual(convo.source, "old context")
        self.assertIn("resumed", convo.handle("Metal: resume").speech)
        self.assertIsNotNone(convo.handle("Metal: explain").question)

    def test_missing_backend_is_honest_and_common_error_still_has_advice(self):
        convo = Conversation()
        self.assertIn("not found", convo.handle("Metal: codex").speech)
        self.assertIn("Built-in replies", convo.handle("Metal: what next?").speech)
        reply = convo.local_reply(Question("What should I do?", "TypeError: bad type", ()),
                                 "The service failed.")
        self.assertEqual(reply.kind, "retry")
        self.assertIn("service failed", reply.message)
        self.assertIn("types", reply.message)
        self.assertNotIn("fixed", reply.message)

    def test_forget_and_local_mode_still_work_while_quiet(self):
        convo = Conversation(codex_available=True)
        convo.handle("TypeError: example")
        question = convo.handle("Metal: explain").question
        convo.finish(question, Reply("retry", "Check the value's type."))
        convo.handle("Metal: quiet")
        self.assertEqual(convo.handle("Metal: forget").speech, "")
        self.assertEqual(convo.source, "")
        self.assertEqual(convo.history, [])
        self.assertEqual(convo.handle("Metal: local").speech, "")
        self.assertFalse(convo.use_codex)
        self.assertTrue(convo.muted)


if __name__ == "__main__":
    unittest.main()
