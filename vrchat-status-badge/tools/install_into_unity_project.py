#!/usr/bin/env python3
"""Install Overhead Status Badge as a local Unity package."""

import argparse
import json
from pathlib import Path
import sys


PACKAGE_NAME = "com.local.overhead-status-badge"


def main() -> int:
    parser = argparse.ArgumentParser(description="Add this package to a Unity project manifest.")
    parser.add_argument("project", help="Path to the Unity project root.")
    args = parser.parse_args()

    package_root = Path(__file__).resolve().parents[1]
    project_root = Path(args.project).expanduser().resolve()
    manifest_path = project_root / "Packages" / "manifest.json"

    if not manifest_path.exists():
        print(f"not a Unity project or missing manifest: {manifest_path}", file=sys.stderr)
        return 2

    with manifest_path.open("r", encoding="utf-8") as handle:
        manifest = json.load(handle)

    dependencies = manifest.setdefault("dependencies", {})
    dependencies[PACKAGE_NAME] = "file:" + str(package_root)

    with manifest_path.open("w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2)
        handle.write("\n")

    print(f"installed {PACKAGE_NAME} into {manifest_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

