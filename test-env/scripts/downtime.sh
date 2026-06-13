#!/usr/bin/env bash
# Make a service CRITICAL and put it in a 1h downtime (default: svc-dt).
# The firmware filter (in_downtime=n) must IGNORE it.
set -euo pipefail
cd "$(dirname "$0")"; . ./_lib.sh
SVC="${1:-svc-dt}"
NOW=$(date +%s); END=$((NOW + 3600))
echo "Setting ${HOST}!${SVC} CRITICAL + downtime (1h)..."
set_state "$SVC" 2 "CRITICAL: to be put in downtime"
icinga_post "actions/schedule-downtime" \
  "{\"type\":\"Service\",\"filter\":\"service.name==\\\"${SVC}\\\" && host.name==\\\"${HOST}\\\"\",\"author\":\"tester\",\"comment\":\"muted by downtime.sh\",\"start_time\":${NOW},\"end_time\":${END},\"fixed\":true,\"duration\":3600}" \
  >/dev/null
echo "  downtime scheduled."
