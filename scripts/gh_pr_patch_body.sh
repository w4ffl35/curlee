#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/gh_pr_patch_body.sh <pr-number> <body-file>

Workaround for `gh pr edit` GraphQL failures in repos that still have
legacy (classic) project cards.

This updates the PR body via the REST API:
  PATCH /repos/{owner}/{repo}/pulls/{pull_number}

Examples:
  scripts/gh_pr_patch_body.sh 123 /tmp/new-body.md
EOF
}

if [[ ${1:-} == "-h" || ${1:-} == "--help" ]]; then
  usage
  exit 0
fi

if [[ $# -ne 2 ]]; then
  usage >&2
  exit 2
fi

pr_number="$1"
body_file="$2"

if [[ ! "$pr_number" =~ ^[0-9]+$ ]]; then
  echo "error: pr-number must be an integer" >&2
  exit 2
fi

if [[ ! -f "$body_file" ]]; then
  echo "error: body-file does not exist: $body_file" >&2
  exit 2
fi

repo=$(gh repo view --json nameWithOwner --jq .nameWithOwner)

python3 - <<'PY' "$repo" "$pr_number" "$body_file" | gh api \
  --method PATCH \
  --input - \
  "repos/$repo/pulls/$pr_number" \
  >/dev/null
import json
import sys

repo = sys.argv[1]
pr_number = int(sys.argv[2])
body_file = sys.argv[3]

with open(body_file, 'r', encoding='utf-8') as f:
    body = f.read()

# `gh api --input -` expects a JSON object.
print(json.dumps({"body": body}))
PY

echo "ok: updated PR #$pr_number body via REST (repo=$repo)"
