#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
IP="${1:-127.0.0.1}"
PORT="${2:-6000}"
if [ -f "$ROOT_DIR/config/server.env" ]; then
  # shellcheck disable=SC1091
  source "$ROOT_DIR/config/server.env"
fi
exec "$ROOT_DIR/bin/ChatServer" "$IP" "$PORT"
