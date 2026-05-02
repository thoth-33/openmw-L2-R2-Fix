#!/bin/bash

set -euo pipefail

attempts="${1:-10}"
sleep_seconds="${2:-5}"

if [[ "$attempts" -lt 1 ]]; then
  echo "error: attempts must be >= 1" >&2
  exit 2
fi

for ((attempt = 1; attempt <= attempts; ++attempt)); do
  echo "apt-get update attempt ${attempt}/${attempts}"

  apt-get clean || true
  rm -rf /var/lib/apt/lists/*

  if apt-get update -o Acquire::Retries=3; then
    exit 0
  fi

  if [[ "$attempt" -lt "$attempts" ]]; then
    sleep "$sleep_seconds"
  fi
done

echo "apt-get update failed after ${attempts} attempts" >&2
exit 1

