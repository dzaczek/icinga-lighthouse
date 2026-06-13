#!/usr/bin/env bash
# Drive a service into a flapping state by alternating OK/CRITICAL several
# times (default: svc-flap). The firmware filter (is_flapping=n) must IGNORE it.
# Leaves the service CRITICAL at the end so only the flapping flag (not the
# state) would otherwise make it eligible.
set -euo pipefail
cd "$(dirname "$0")"; . ./_lib.sh
SVC="${1:-svc-flap}"
echo "Flapping ${HOST}!${SVC} (alternating ~16x)..."
for i in $(seq 1 16); do
  if (( i % 2 == 0 )); then set_state "$SVC" 0 "flap OK $i"; else set_state "$SVC" 2 "flap CRIT $i"; fi
  sleep 0.5
done
set_state "$SVC" 2 "CRITICAL while flapping"
echo "  done — check is_flapping in icingadb-web."
