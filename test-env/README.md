# Icinga Lighthouse Test Environment

This environment simulates an Icinga 2 infrastructure and provides instructions for running a "Virtual ESP32" to test your Arduino code without physical hardware.

## 1. Icinga Environment (Podman)

The Icinga 2 stack is isolated in the `icinga` profile.

### Start Icinga
```bash
cd test-env
podman-compose --in-pod false --profile icinga up -d
```

### Access
- **Icinga Web 2 (UI)**: [http://localhost:8080](http://localhost:8080) (User: `admin`, Pass: `admin`)
- **Icinga 2 API**: `https://localhost:5665` (User: `root`, Pass: `icinga`)

### Icinga Web 2: first-time setup wizard (required)
If you see a message like “you did not configure Icinga Web 2 yet…”, run the setup wizard once.

1. Generate a setup token:
```bash
podman exec icingaweb2 icingacli setup token create
```

2. Open the wizard:
- `http://localhost:8080/setup`

3. In the wizard use MariaDB connection for **Icinga Web 2 configuration/auth DB**:
- **Host**: `icingadb`
- **Database**: `icingaweb2`
- **User**: `icingaweb2`
- **Password**: `icingaweb2`

The database/user is created automatically on first DB init via `test-env/icingadb-init/00-icingaweb2.sql`.

### First run note (API password)
On the first start, the Icinga2 container generates a random password in `/data/etc/icinga2/conf.d/api-users.conf`.
For this repo we use a bind-mounted `/data` directory (`test-env/data/icinga2`), so you can change it on the host if needed.

### Stop Icinga
```bash
podman-compose --in-pod false --profile icinga down
```

---

## 2. Virtual ESP32 (container emulator + web UI)

We provide a **containerized ESP32 emulator** (Linux simulation) that runs next to Icinga in Podman Compose.

It compiles your `trelaylaatern.ino` inside a container (profile `sim`) using a small Arduino compatibility layer, and prints relay state changes to the logs.

### Start Virtual ESP32
```bash
cd test-env
podman-compose --in-pod false --profile sim up --build
```

### Web UI (from your browser)
The simulated device exposes the same web UI as the real ESP32 sketch:

- `http://localhost:8081/`

Credentials are the same as in the sketch:
- User: `admin`
- Pass: `admin`

Example with curl:
```bash
curl -u admin:admin http://localhost:8081/ | head
```

### Run both (Icinga + Virtual ESP32)
```bash
cd test-env
podman-compose --in-pod false --profile icinga up -d
podman-compose --in-pod false --profile sim up --build
```

### Run everything with one command
```bash
cd test-env
podman-compose --in-pod false --profile icinga --profile sim up -d --build
```

### What you should see
- In `esp32-sim` logs you will see relay events like: `[GPIO] RELAY Pin 21 -> ON`
- The status LED pin (25) will blink in logs: `[GPIO] Pin 25 -> 1/0`

### Notes
- The simulator connects to Icinga using the hostname `icinga2` on the shared compose network.
- This is not a full hardware emulator (no real ESP32 peripherals), but it runs your alarm logic + HTTP polling and is great for fast iterations.

---

## 3. Testing Scenarios (Triggering Alarms)

Use `curl` commands to change the state of the monitored service in Icinga. The Virtual ESP32 should react within its polling interval (default 5s).

**Trigger Critical Alarm (Relay ON):**
```bash
curl -k -u root:icinga -H 'Accept: application/json' -X POST \
 'https://localhost:5665/v1/actions/process-check-result?service=test-host!test-service' \
 -d '{ "exit_status": 2, "plugin_output": "CRITICAL: Test Failure", "check_source": "manual" }'
```

**Reset to OK (Relay OFF):**
```bash
curl -k -u root:icinga -H 'Accept: application/json' -X POST \
 'https://localhost:5665/v1/actions/process-check-result?service=test-host!test-service' \
 -d '{ "exit_status": 0, "plugin_output": "OK: Test Passed", "check_source": "manual" }'
```

If `test-host` / `test-service` don't exist yet, create them first:
```bash
curl -k -u root:icinga -H 'Accept: application/json' -X PUT \
  'https://localhost:5665/v1/objects/hosts/test-host' \
  -d '{ "attrs": { "address": "127.0.0.1", "check_command": "hostalive" } }'

curl -k -u root:icinga -H 'Accept: application/json' -X PUT \
  'https://localhost:5665/v1/objects/services/test-host!test-service' \
  -d '{ "attrs": { "check_command": "dummy", "vars.dummy_state": 0, "vars.dummy_text": "OK" } }'
```
