#!/usr/bin/env bash
# Initial Git setup for a local VENTUS development checkout.
# Deliberately does NOT commit, push, or remove files.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if ! command -v git >/dev/null 2>&1; then
    echo 'Git is required (sudo apt install git).' >&2
    exit 2
fi

if ! git rev-parse --show-toplevel >/dev/null 2>&1; then
    git init -b main
    echo 'Initialized local Git repository on main.'
else
    echo 'Existing Git repository detected; preserving its branch and history.'
fi

# Detect already tracked external data before staging anything new.
python3 tools/check_repo_hygiene.py --tracked

git add -A
python3 tools/check_repo_hygiene.py --staged

echo
echo 'Staged files:'
git diff --cached --stat
echo
echo 'Review with: git diff --cached --name-only'
echo 'When satisfied: git commit -m "Initial qVENTUS development baseline with native CLI"'
echo 'No commit or push has been made.'
