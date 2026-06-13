#!/usr/bin/env bash
# Seed the full test matrix in one go. Run once after the stack is healthy.
#   svc-ok   -> OK
#   svc-crit -> OK   (use set-critical.sh to trigger the siren)
#   svc-ack  -> CRITICAL + acknowledged   (ignored)
#   svc-dt   -> CRITICAL + downtime        (ignored)
#   svc-flap -> flapping + CRITICAL        (ignored)
set -euo pipefail
cd "$(dirname "$0")"; . ./_lib.sh

echo "Seeding baseline states..."
set_state svc-ok   0 "seed OK"
set_state svc-crit 0 "seed OK (trigger with set-critical.sh)"

echo "Seeding handled CRITICALs (must be ignored by the firmware)..."
./ack.sh      svc-ack
./downtime.sh svc-dt
./flap.sh     svc-flap

echo
echo "Seed complete. At rest only an explicit set-critical.sh should fire the siren."
