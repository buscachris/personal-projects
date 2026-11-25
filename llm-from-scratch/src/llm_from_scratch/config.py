from dataclasses import asdict, dataclass
from typing import Any


@dataclass
class ModelConfig:
    vocab_size: int = 259
    max_seq_len: int = 512
    d_model: int = 512
    n_layers: int = 8
    n_heads: int = 8
    n_kv_heads: int = 8
    d_ff: int = 2048
    dropout: float = 0.0
    rope_base: float = 10_000.0

    def __post_init__(self) -> None:
        if self.d_model % self.n_heads:
            raise ValueError("d_model must be divisible by n_heads")
        if self.n_heads % self.n_kv_heads:
            raise ValueError("n_heads must be divisible by n_kv_heads")
        if self.d_model // self.n_heads % 2:
            raise ValueError("head dimension must be even for rotary embeddings")
        if self.max_seq_len < 1 or self.vocab_size < 1:
            raise ValueError("vocab_size and max_seq_len must be positive")

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass
class TrainingConfig:
    batch_size: int = 4
    grad_accum_steps: int = 8
    learning_rate: float = 3e-4
    weight_decay: float = 0.1
    warmup_steps: int = 100
    max_steps: int = 1_000
    grad_clip: float = 1.0
    seed: int = 1337
    save_every: int = 250
