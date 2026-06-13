#!/usr/bin/env bash
# One-shot initialiser, run by the init-icinga2 container.
#
# The icinga/icinga2 image ENTRYPOINT runs first (node setup populates /data),
# then execs this script. We drop our extra config into the initialised /data
# volume so the real icinga2 container picks it up on start.
set -euo pipefail

CONF_D="/data/etc/icinga2/conf.d"
FEATURES="/data/etc/icinga2/features-enabled"

mkdir -p "$CONF_D" "$FEATURES"

# Enable the Icinga DB feature (publish to redis).
cp -f /config/icingadb.conf "$FEATURES/icingadb.conf"

# Known API user (root/icinga) — overwrite the random one the image may create.
cp -f /config/api-users.conf "$CONF_D/api-users.conf"

# Test host + services.
cp -f /config/test-objects.conf "$CONF_D/test-objects.conf"

# Drop the image's example objects (NodeName host + http/ssh/mysql/swap/...
# applied services + dummy-service-critical) so ONLY our test matrix exists.
# Otherwise those default CRITICAL services would constantly trip the siren.
rm -f "$CONF_D/hosts.conf" "$CONF_D/services.conf" "$CONF_D/dummy.conf"

echo "[init-icinga2] config staged into /data:"
ls -l "$CONF_D" "$FEATURES"
