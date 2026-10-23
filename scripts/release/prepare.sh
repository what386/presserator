#!/usr/bin/env bash
set -euo pipefail

if [[ $# != 1 ]]; then
    echo "Usage: prepare <version>" >&2
    exit 1
fi
if [[ ! -r .release-state ]] || [[ "$(<.release-state)" != "ready" ]]; then
    echo "Invalid release state: should be 'ready'" >&2
    exit 1
fi
if [[ "$(git branch --show-current)" != "dev" ]] || [[ -n "$(git status --porcelain)" ]]; then
    echo "Branch must be dev and the working tree clean" >&2
    exit 1
fi

just verify
tally semver "$1"
git add CHANGELOG.md TODO.md
if ! git diff --cached --quiet; then
    git commit -m "Update changelog for release $1"
fi
printf "prepared" > .release-state
