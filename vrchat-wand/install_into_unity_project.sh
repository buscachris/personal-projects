#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: $0 /path/to/UnityProject" >&2
  exit 2
fi

project_path="$1"
assets_path="$project_path/Assets"

if [ ! -d "$project_path" ]; then
  echo "Project path does not exist: $project_path" >&2
  exit 1
fi

if [ ! -d "$assets_path" ]; then
  echo "Target does not look like a Unity project; missing Assets directory: $assets_path" >&2
  exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_path="$script_dir/Assets/PatrioticWand"
target_path="$assets_path/PatrioticWand"

if [ ! -d "$source_path" ]; then
  echo "Package source is missing: $source_path" >&2
  exit 1
fi

mkdir -p "$target_path"
cp -a "$source_path/." "$target_path/"

echo "Installed PatrioticWand into $target_path"
echo "Open Unity and run Tools > Patriotic Wand > Create World Pickup Prefab"
