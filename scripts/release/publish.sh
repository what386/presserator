#!/usr/bin/env bash
set -euo pipefail

if [[ $# != 1 ]]; then
    echo "Usage: publish <version>" >&2
    exit 1
fi
if [[ ! -r .release-state ]] || [[ "$(<.release-state)" != "promoted" ]]; then
    echo "Invalid release state: should be 'promoted'" >&2
    exit 1
fi
if [[ "$(git branch --show-current)" != "main" ]] || [[ -n "$(git status --porcelain)" ]]; then
    echo "Branch must be main and the working tree clean" >&2
    exit 1
fi
if [[ -n "$(git tag --list "$1")" ]]; then
    echo "Tag $1 already exists" >&2
    exit 1
fi

just verify-release
git tag "$1"
git push origin "$1"
printf "published" > .release-state
