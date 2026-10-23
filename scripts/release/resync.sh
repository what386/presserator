#!/usr/bin/env bash
set -euo pipefail

if [[ $# != 0 ]]; then
    echo "Usage: resync" >&2
    exit 1
fi
if [[ "$(git branch --show-current)" != "dev" ]]; then
    echo "Branch must be dev" >&2
    exit 1
fi

git push origin dev
git switch main
git merge dev -m "Merge dev into main"
git push origin main
git switch dev
