#!/usr/bin/env bash
# Set a service CRITICAL (default: svc-crit). Should make the siren fire
# after the configured confirm-threshold of polls.
set -euo pipefail
cd "$(dirname "$0")"; . ./_lib.sh
SVC="${1:-svc-crit}"
echo "Setting ${HOST}!${SVC} CRITICAL..."
set_state "$SVC" 2 "CRITICAL: forced by set-critical.sh"
