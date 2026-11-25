import argparse
import json
import os
import random
import time
from dataclasses import asdict
from pathlib import Path

import torch
from torch.nn.parallel import DistributedDataParallel
from torch.utils.data import DataLoader, DistributedSampler

from .config import ModelConfig, TrainingConfig
from .data import InstructionDataset, collate_batch, load_jsonl, synthetic_examples
from .model import GPTModel
from .tokenizer import ByteTokenizer


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Train the GPT instruction-tuning scaffold")
    parser.add_argument("--data", type=Path)
    parser.add_argument("--synthetic", action="store_true")
    parser.add_argument("--output", type=Path, default=Path("runs/smoke"))
    parser.add_argument("--steps", type=int, default=10)
    parser.add_argument("--batch-size", type=int, default=2)
    parser.add_argument("--grad-accum", type=int, default=1)
    parser.add_argument("--max-seq-len", type=int, default=256)
    parser.add_argument("--d-model", type=int, default=128)
    parser.add_argument("--layers", type=int, default=2)
    parser.add_argument("--heads", type=int, default=4)
    parser.add_argument("--lr", type=float, default=3e-4)
    parser.add_argument("--save-every", type=int, default=10)
    parser.add_argument("--resume", type=Path)
    return parser.parse_args()


def setup_distributed() -> tuple[bool, int, int]:
    world_size = int(os.environ.get("WORLD_SIZE", "1"))
    if world_size == 1:
        return False, 0, 0
    local_rank = int(os.environ["LOCAL_RANK"])
    torch.cuda.set_device(local_rank)
    torch.distributed.init_process_group("nccl")
    return True, local_rank, torch.distributed.get_rank()


def save_checkpoint(
    path: Path,
    model: torch.nn.Module,
    optimizer: torch.optim.Optimizer,
    step: int,
    model_config: ModelConfig,
    training_config: TrainingConfig,
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    unwrapped = model.module if isinstance(model, DistributedDataParallel) else model
    payload = {
        "step": step,
        "model": unwrapped.state_dict(),
        "optimizer": optimizer.state_dict(),
        "model_config": model_config.to_dict(),
        "training_config": asdict(training_config),
    }
    temporary = path.with_suffix(".tmp")
    torch.save(payload, temporary)
    os.replace(temporary, path)


def main() -> None:
    args = parse_args()
    distributed, local_rank, rank = setup_distributed()
    device = torch.device(f"cuda:{local_rank}" if distributed else ("cuda" if torch.cuda.is_available() else "cpu"))
    random.seed(1337 + rank)
    torch.manual_seed(1337 + rank)

    model_config = ModelConfig(
        max_seq_len=args.max_seq_len,
        d_model=args.d_model,
        n_layers=args.layers,
        n_heads=args.heads,
        n_kv_heads=args.heads,
        d_ff=args.d_model * 4,
    )
    training_config = TrainingConfig(
        batch_size=args.batch_size,
        grad_accum_steps=args.grad_accum,
        max_steps=args.steps,
        learning_rate=args.lr,
        save_every=args.save_every,
    )
    tokenizer = ByteTokenizer()
    examples = load_jsonl(args.data) if args.data else synthetic_examples()
    dataset = InstructionDataset(examples, tokenizer, model_config.max_seq_len)
    sampler = DistributedSampler(dataset, shuffle=True) if distributed else None
    loader = DataLoader(
        dataset,
        batch_size=training_config.batch_size,
        sampler=sampler,
        shuffle=sampler is None,
        collate_fn=collate_batch,
        pin_memory=device.type == "cuda",
    )
    model = GPTModel(model_config).to(device)
    optimizer = torch.optim.AdamW(
        model.parameters(),
        lr=training_config.learning_rate,
        weight_decay=training_config.weight_decay,
        betas=(0.9, 0.95),
    )
    start_step = 0
    if args.resume:
        checkpoint = torch.load(args.resume, map_location=device, weights_only=False)
        model.load_state_dict(checkpoint["model"])
        optimizer.load_state_dict(checkpoint["optimizer"])
        start_step = int(checkpoint["step"])
    if distributed:
        model = DistributedDataParallel(model, device_ids=[local_rank])

    amp_enabled = device.type == "cuda"
    scaler = torch.amp.GradScaler("cuda", enabled=amp_enabled)
    model.train()
    iterator = iter(loader)
    start_time = time.time()
    for step in range(start_step, training_config.max_steps):
        if sampler is not None and step % len(loader) == 0:
            sampler.set_epoch(step // len(loader))
        optimizer.zero_grad(set_to_none=True)
        total_loss = 0.0
        for _ in range(training_config.grad_accum_steps):
            try:
                batch = next(iterator)
            except StopIteration:
                iterator = iter(loader)
                batch = next(iterator)
            batch = {key: value.to(device) for key, value in batch.items()}
            with torch.autocast(device_type=device.type, dtype=torch.bfloat16, enabled=amp_enabled):
                _, loss = model(batch["input_ids"], batch["labels"])
            if loss is None:
                raise RuntimeError("training requires labels")
            total_loss += loss.item()
            scaler.scale(loss / training_config.grad_accum_steps).backward()
        scaler.unscale_(optimizer)
        torch.nn.utils.clip_grad_norm_(model.parameters(), training_config.grad_clip)
        scaler.step(optimizer)
        scaler.update()
        if rank == 0 and ((step + 1) % 10 == 0 or step == start_step):
            elapsed = time.time() - start_time
            print(f"step={step + 1} loss={total_loss / training_config.grad_accum_steps:.4f} "
                  f"tokens/s={training_config.batch_size * model_config.max_seq_len * training_config.grad_accum_steps / max(elapsed, 1e-6):.0f}",
                  flush=True)
        if rank == 0 and (step + 1) % training_config.save_every == 0:
            save_checkpoint(args.output / f"step-{step + 1}.pt", model, optimizer, step + 1, model_config, training_config)
    if rank == 0:
        save_checkpoint(args.output / "final.pt", model, optimizer, training_config.max_steps, model_config, training_config)
        (args.output / "run.json").write_text(json.dumps({"parameters": model_config.to_dict()}, indent=2), encoding="utf-8")
    if distributed:
        torch.distributed.destroy_process_group()


if __name__ == "__main__":
    main()
