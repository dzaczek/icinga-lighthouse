#!/usr/bin/env bash
# Shared helpers for the test scripts. Talks to the Icinga 2 core API (:5665)
# to *change* object state. The ESP32 firmware itself reads from Icinga Web
# (icingadb-web, :8080) — these scripts only drive the scenario.
set -euo pipefail

API="${ICINGA_API:-https://localhost:5665}"
AUTH="${ICINGA_AUTH:-root:icinga}"
HOST="${TEST_HOST:-test-host}"

icinga_post() { # <path> <json>
  curl -fsS -k -u "$AUTH" \
    -H 'Accept: application/json' \
    -X POST "$API/v1/$1" \
    -d "$2"
}

# Push a passive check result for test-host!<service>.
set_state() { # <service> <exit_status> <text>
  icinga_post "actions/process-check-result?service=${HOST}!$1" \
    "{\"exit_status\": $2, \"plugin_output\": \"$3\", \"check_source\": \"lighthouse-test\"}" \
    >/dev/null
  echo "  $1 -> exit=$2 ($3)"
}
