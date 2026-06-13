#!/usr/bin/env bash
# Make a service CRITICAL and acknowledge it (default: svc-ack).
# The firmware filter (is_acknowledged=n) must IGNORE it.
set -euo pipefail
cd "$(dirname "$0")"; . ./_lib.sh
SVC="${1:-svc-ack}"
echo "Setting ${HOST}!${SVC} CRITICAL + acknowledged..."
set_state "$SVC" 2 "CRITICAL: to be acknowledged"
icinga_post "actions/acknowledge-problem" \
  "{\"type\":\"Service\",\"filter\":\"service.name==\\\"${SVC}\\\" && host.name==\\\"${HOST}\\\"\",\"author\":\"tester\",\"comment\":\"muted by ack.sh\",\"notify\":false}" \
  >/dev/null
echo "  acknowledged."
