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

if git rev-parse -q --verify "refs/tags/$tag" >/dev/null; then
  echo "Tag $tag already exists locally." >&2
  echo "Delete it with: git tag -d $tag" >&2
  echo "Or use the next version tag (recommended) e.g. vX.Y.(Z+1)." >&2
  exit 1
fi

if git ls-remote --tags origin "$tag" | grep -q .; then
  echo "Tag $tag already exists on origin." >&2
  echo "Use a new version tag (recommended), or delete remote tag with:" >&2
  echo "  git push --delete origin $tag" >&2
  exit 1
fi

git tag -a "$tag" -m "$tag"
git push origin "$tag"

echo "Pushed tag $tag. GitHub Actions will build and publish firmware.bin + spiffs.bin to the release."