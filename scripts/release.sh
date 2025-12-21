#!/usr/bin/env bash
set -euo pipefail

# Function to increment version
increment_version() {
    local version="$1"
    # Remove 'v' prefix
    version="${version#v}"
    
    # Split into parts
    local major minor patch
    IFS='.' read -r major minor patch <<< "$version"
    
    # Increment patch version
    patch=$((patch + 1))
    
    echo "v${major}.${minor}.${patch}"
}

# Get tag from argument or auto-increment from last tag
if [[ $# -eq 0 ]]; then
    # Get latest tag from git
    last_tag=$(git describe --tags --abbrev=0 2>/dev/null || echo "")
    
    if [[ -z "$last_tag" ]]; then
        echo "No existing tags found. Please provide initial version: $0 v1.0.0" >&2
        exit 2
    fi
    
    tag=$(increment_version "$last_tag")
    echo "Auto-incrementing from $last_tag to $tag"
elif [[ $# -eq 1 ]]; then
    tag="$1"
else
    echo "Usage: $0 [vX.Y.Z]" >&2
    echo "  If no version provided, auto-increments patch from last tag" >&2
    exit 2
fi

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