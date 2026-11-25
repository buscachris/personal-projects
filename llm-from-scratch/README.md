# LLM from scratch

I use this small PyTorch language model to study the path from an instruction example to a generated response. I prepare the data, mask the prompt during fine-tuning, and save checkpoints that I can reload for generation.

The default model is intentionally small. Increase `d_model`, `layers`,
`heads`, `max_seq_len`, and the dataset size for real experiments. For serious
pretraining, replace `ByteTokenizer` with a trained BPE/SentencePiece
tokenizer, add a deduplicated/licensed corpus, and provision multi-GPU storage
and monitoring.

## Run the CPU smoke test

```bash
cd llm-from-scratch
python -m venv .venv
.venv/bin/pip install --index-url https://download.pytorch.org/whl/cpu torch
.venv/bin/pip install -e '.[dev]' --no-deps
.venv/bin/python -m llm_from_scratch.train --synthetic --steps 10 --output runs/smoke
python -m llm_from_scratch.generate runs/smoke/final.pt \
  --prompt $'<|user|>\nGive a short greeting.\n<|assistant|>\n' --tokens 16
.venv/bin/pytest -q
```

## Use JSONL instruction data

Each non-empty line must contain `instruction` and `output`; `input` is
optional:

```json
{"instruction":"Summarize this text.","input":"A short document.","output":"A concise summary."}
```

```bash
python -m llm_from_scratch.train --data data/train.jsonl \
  --steps 10000 --batch-size 8 --grad-accum 8 --output runs/experiment
```

For multi-GPU execution, launch the same entry point with `torchrun`; the
trainer detects `WORLD_SIZE` and wraps the model in DDP:

```bash
torchrun --standalone --nproc_per_node=8 -m llm_from_scratch.train \
  --data data/train.jsonl --steps 10000 --output runs/experiment
```

Checkpoints contain model configuration, optimizer state, and the completed
step, so interrupted runs can be resumed with `--resume`.
