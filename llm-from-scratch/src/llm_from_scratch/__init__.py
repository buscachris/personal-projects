"""Configurable GPT-style language-model training scaffold."""

from .config import ModelConfig
from .model import GPTModel

__all__ = ["GPTModel", "ModelConfig"]
