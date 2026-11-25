import json
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

import torch
from torch.utils.data import Dataset

from .tokenizer import ByteTokenizer


@dataclass(frozen=True)
class InstructionExample:
    instruction: str
    output: str
    input: str = ""


def synthetic_examples() -> list[InstructionExample]:
    return [
        InstructionExample("Add two numbers.", "The sum is 5.", "2 and 3"),
        InstructionExample("Translate to French.", "Bonjour le monde.", "Hello world."),
        InstructionExample("Name a primary color.", "Blue."),
        InstructionExample("Give a short greeting.", "Hello!"),
    ]


def load_jsonl(path: str | Path) -> list[InstructionExample]:
    examples: list[InstructionExample] = []
    with Path(path).open(encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, 1):
            if not line.strip():
                continue
            try:
                row = json.loads(line)
                instruction = row["instruction"]
                output = row.get("output")
                if output is None:
                    output = row["response"]
                user_input = row.get("input", "")
            except (KeyError, TypeError, json.JSONDecodeError) as exc:
                raise ValueError(f"invalid instruction record at line {line_number}") from exc
            if not isinstance(instruction, str) or not isinstance(output, str):
                raise ValueError(f"instruction and output must be strings at line {line_number}")
            examples.append(InstructionExample(instruction, output, user_input))
    if not examples:
        raise ValueError(f"no examples found in {path}")
    return examples


def render_prompt(example: InstructionExample) -> str:
    prompt = f"<|user|>\n{example.instruction}"
    if example.input:
        prompt += f"\n\n{example.input}"
    return prompt + "\n<|assistant|>\n"


class InstructionDataset(Dataset[dict[str, torch.Tensor]]):
    def __init__(
        self,
        examples: Iterable[InstructionExample],
        tokenizer: ByteTokenizer,
        max_seq_len: int,
    ) -> None:
        self.records: list[dict[str, torch.Tensor]] = []
        for example in examples:
            prompt_ids = tokenizer.encode(render_prompt(example), add_bos=True)
            response_ids = tokenizer.encode(example.output, add_bos=False, add_eos=True)
            ids = (prompt_ids + response_ids)[:max_seq_len]
            labels = ([-100] * len(prompt_ids) + response_ids)[:max_seq_len]
            if any(label != -100 for label in labels):
                self.records.append(
                    {
                        "input_ids": torch.tensor(ids, dtype=torch.long),
                        "labels": torch.tensor(labels, dtype=torch.long),
                    }
                )
        if not self.records:
            raise ValueError("all examples exceed max_seq_len before their response")

    def __len__(self) -> int:
        return len(self.records)

    def __getitem__(self, index: int) -> dict[str, torch.Tensor]:
        return self.records[index]


def collate_batch(
    batch: list[dict[str, torch.Tensor]], pad_token_id: int = ByteTokenizer.PAD
) -> dict[str, torch.Tensor]:
    max_len = max(item["input_ids"].numel() for item in batch)
    input_ids = torch.full((len(batch), max_len), pad_token_id, dtype=torch.long)
    labels = torch.full((len(batch), max_len), -100, dtype=torch.long)
    for row, item in enumerate(batch):
        length = item["input_ids"].numel()
        input_ids[row, :length] = item["input_ids"]
        labels[row, :length] = item["labels"]
    return {"input_ids": input_ids, "labels": labels}
