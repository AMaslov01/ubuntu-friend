#!/usr/bin/env bash
set -euo pipefail
IFS=$'\n\t'

# Build project with meson
echo "Configuring and building project with meson..."
meson setup build --wipe || meson setup build

# Compile project
meson compile -C build

# Run unit tests (gtest harness)
echo "Running tests..."

# If token preprovided (e.g. in CI variables), prefer it.
if [[ -n "${NETWORKFS_TOKEN:-}" ]]; then
  echo "Using provided NETWORKFS_TOKEN from environment."
else
  echo "Requesting a fresh token from the server..."
  resp=$(curl -sS "https://nerc.itmo.ru/teaching/os/networkfs/v1/token/issue?json")
  token=$(echo "$resp" | jq -r '.response // empty')
  if [[ -z "$token" ]]; then
    echo "Failed to obtain token from server. Response:"
    echo "$resp"
    exit 2
  fi
  export NETWORKFS_TOKEN=$token
  echo "Obtained NETWORKFS_TOKEN: ${NETWORKFS_TOKEN}"
fi

mkdir -p /mnt/networkfs-test
meson test -C build --verbose --no-rebuild
