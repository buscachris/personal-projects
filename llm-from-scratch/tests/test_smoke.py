import torch

from llm_from_scratch.config import ModelConfig
from llm_from_scratch.data import InstructionDataset, collate_batch, synthetic_examples
from llm_from_scratch.model import GPTModel
from llm_from_scratch.tokenizer import ByteTokenizer


def test_forward_backward_and_generation() -> None:
    config = ModelConfig(max_seq_len=64, d_model=32, n_layers=2, n_heads=4, n_kv_heads=2, d_ff=128)
    model = GPTModel(config)
    dataset = InstructionDataset(synthetic_examples(), ByteTokenizer(), config.max_seq_len)
    batch = collate_batch([dataset[0], dataset[1]])
    logits, loss = model(batch["input_ids"], batch["labels"])
    assert logits.shape[:2] == batch["input_ids"].shape
    assert loss is not None and torch.isfinite(loss)
    loss.backward()
    generated = model.generate(batch["input_ids"][:, :4], max_new_tokens=3)
    assert generated.shape[1] == 7
