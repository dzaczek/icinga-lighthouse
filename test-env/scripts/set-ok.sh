#!/usr/bin/env bash
# Set a service back to OK (default: svc-crit). Siren should clear.
set -euo pipefail
cd "$(dirname "$0")"; . ./_lib.sh
SVC="${1:-svc-crit}"
echo "Setting ${HOST}!${SVC} OK..."
set_state "$SVC" 0 "OK: forced by set-ok.sh"
