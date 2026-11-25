import argparse
from pathlib import Path

import torch

from .config import ModelConfig
from .model import GPTModel
from .tokenizer import ByteTokenizer


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("checkpoint", type=Path)
    parser.add_argument("--prompt", required=True)
    parser.add_argument("--tokens", type=int, default=64)
    parser.add_argument("--temperature", type=float, default=0.8)
    args = parser.parse_args()

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    checkpoint = torch.load(args.checkpoint, map_location=device, weights_only=False)
    config = ModelConfig(**checkpoint["model_config"])
    model = GPTModel(config).to(device)
    model.load_state_dict(checkpoint["model"])
    tokenizer = ByteTokenizer()
    input_ids = torch.tensor([tokenizer.encode(args.prompt)], dtype=torch.long, device=device)
    output = model.generate(input_ids, args.tokens, args.temperature)
    print(tokenizer.decode(output[0].tolist()))


if __name__ == "__main__":
    main()
