class ByteTokenizer:
    """Deterministic dependency-free tokenizer for smoke tests and bootstrapping.

    Replace this with a trained BPE or SentencePiece tokenizer before serious
    training. Keeping the tokenizer explicit makes checkpoint vocabularies
    reproducible.
    """

    PAD = 256
    BOS = 257
    EOS = 258
    vocab_size = 259

    def encode(self, text: str, *, add_bos: bool = True, add_eos: bool = False) -> list[int]:
        tokens = list(text.encode("utf-8"))
        if add_bos:
            tokens.insert(0, self.BOS)
        if add_eos:
            tokens.append(self.EOS)
        return tokens

    def decode(self, tokens: list[int]) -> str:
        raw = bytes(token for token in tokens if 0 <= token < 256)
        return raw.decode("utf-8", errors="replace")
