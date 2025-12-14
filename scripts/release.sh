#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 vX.Y.Z" >&2
  exit 2
fi

tag="$1"

if [[ ! "$tag" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Tag must look like vX.Y.Z (example: v1.2.3)" >&2
  exit 2
fi

git diff --quiet || { echo "Working tree is dirty. Commit/stash first." >&2; exit 1; }

git tag -a "$tag" -m "$tag"
git push origin "$tag"

echo "Pushed tag $tag. GitHub Actions will build and publish firmware.bin + spiffs.bin to the release."