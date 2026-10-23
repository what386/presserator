#!/usr/bin/env bash
set -euo pipefail

if [[ $# != 0 ]]; then
    echo "Usage: promote" >&2
    exit 1
fi
if [[ ! -r .release-state ]] || [[ "$(<.release-state)" != "prepared" ]]; then
    echo "Invalid release state: should be 'prepared'" >&2
    exit 1
fi
if [[ "$(git branch --show-current)" != "dev" ]] || [[ -n "$(git status --porcelain)" ]]; then
    echo "Branch must be dev and the working tree clean" >&2
    exit 1
fi

just verify-release
git push origin dev
git switch main
git merge dev -m "Merge dev into main"
git push origin main
printf "promoted" > .release-state
