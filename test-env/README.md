# Icinga Lighthouse Test Environment

This folder contains a Docker/Podman configuration to run a local Icinga 2 instance for testing the `icinga-lighthouse` Arduino project.

## Prerequisites

- **Docker** or **Podman** installed.
- **docker-compose** or **podman-compose** installed.

## Setup

1. Open a terminal in this directory (`test-env`).
2. Start the environment:
   ```bash
   podman-compose up -d
   # OR
   docker-compose up -d
   ```
3. Wait for containers to start (it may take a minute).

## Access

- **Icinga Web 2 (UI)**: [http://localhost:8080](http://localhost:8080)
  - User: `admin`
  - Password: `admin`
- **Icinga 2 API**: `https://localhost:5665`
  - User: `root`
  - Password: `icinga`

## Configuring the Arduino

1. Connect your Arduino to the same network as your computer.
2. Open the serial console or web configuration of the Arduino.
3. Set **Icinga Host** to your computer's local IP address (e.g., `192.168.1.x`), NOT `localhost`.
4. The ports and credentials in the Arduino sketch match this test environment default:
   - Port: `5665`
   - User: `root`
   - Password: `icinga`
   - URL Service: `.../v1/objects/services?filter=service.state==2`
   - URL Host: `.../v1/objects/hosts?filter=host.state!=0`

## Testing Scenarios

We use `curl` to send commands to the Icinga API. You can run these commands from your computer's terminal.

### 1. Trigger Service CRITICAL (Red Alarm)

This simulates a service going into CRITICAL state.

```bash
curl -k -u root:icinga -H 'Accept: application/json' -X POST \
 'https://localhost:5665/v1/actions/process-check-result?service=test-host!test-service' \
 -d '{ "exit_status": 2, "plugin_output": "CRITICAL: Test Failure", "check_source": "manual" }'
```

### 2. Reset Service to OK (Normal)

This returns the service to OK state, stopping the alarm.

```bash
curl -k -u root:icinga -H 'Accept: application/json' -X POST \
 'https://localhost:5665/v1/actions/process-check-result?service=test-host!test-service' \
 -d '{ "exit_status": 0, "plugin_output": "OK: Test Passed", "check_source": "manual" }'
```

### 3. Trigger Host DOWN (Critical Alarm)

This simulates a host going offline.

```bash
curl -k -u root:icinga -H 'Accept: application/json' -X POST \
 'https://localhost:5665/v1/actions/process-check-result?host=test-host' \
 -d '{ "exit_status": 1, "plugin_output": "DOWN: Host Unreachable", "check_source": "manual" }'
```

### 4. Reset Host to UP

```bash
curl -k -u root:icinga -H 'Accept: application/json' -X POST \
 'https://localhost:5665/v1/actions/process-check-result?host=test-host' \
 -d '{ "exit_status": 0, "plugin_output": "UP: Host Reachable", "check_source": "manual" }'
```

### 5. Schedule Downtime

To ignore alarms for a specific host (Maintenance Mode). This example schedules downtime for 1 hour starting now.

**Note**: You need to insert the current timestamp. If using Linux/macOS:

```bash
NOW=$(date +%s)
END=$(($NOW + 3600))
curl -k -u root:icinga -H 'Accept: application/json' -X POST \
 'https://localhost:5665/v1/actions/schedule-downtime?host=test-host' \
 -d "{ \"start_time\": $NOW, \"end_time\": $END, \"author\": \"tester\", \"comment\": \"Testing Downtime\" }"
```

You can verify the downtime in the Icinga Web 2 interface.

## Cleanup

To stop and remove the environment:

```bash
podman-compose down
# OR
docker-compose down
```

