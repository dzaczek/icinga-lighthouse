# icinga-lighthouse — test environment (Icinga DB stack)

A full, self-contained **Icinga DB** stack plus a **virtual ESP32** that runs the
real `trelaylaatern.ino` firmware compiled for Linux. It lets you verify the
alarm logic end-to-end without any hardware.

The firmware polls **Icinga Web** (the `icingadb-web` JSON API), **not** the
Icinga 2 core API. That is what makes acknowledged / in-downtime / flapping
problems disappear from its view — they are filtered out server-side.

```
icinga2 ──writes──▶ redis ──drained by──▶ icingadb daemon ──▶ MariaDB
                                                                  │
   icingaweb2 (icingadb-web)  ◀── reads redis + MariaDB ──────────┘
        ▲
        │  GET /icingadb/services?...&Accept: application/json
   esp32-sim  (icinga-lighthouse firmware)
```

## Services

| Container        | Role                                   | Host port |
| ---------------- | -------------------------------------- | --------- |
| `il-mariadb`     | MariaDB (icingadb + icingaweb schemas) | –         |
| `il-redis`       | Redis bus between icinga2 and icingadb | –         |
| `il-icinga2`     | Icinga 2 core (API + test objects)     | 5665      |
| `il-icingadb`    | Icinga DB daemon (redis → MariaDB)     | –         |
| `il-icingaweb2`  | Icinga Web 2 + `icingadb` module       | **8080**  |
| `il-esp32-sim`   | Virtual ESP32 running the firmware     | 8081      |

- **Icinga Web 2:** http://localhost:8080  — login `admin` / `admin`
- **Icinga 2 API:** https://localhost:5665 — `root` / `icinga`
- **icingadb-web JSON API base:** `http://localhost:8080/icingadb` (served at `/`, *not* `/icingaweb2`)

## 1. Start the monitoring stack

```bash
cd test-env
docker-compose --profile icinga up -d --build      # or: podman-compose --profile icinga up -d
```

Wait until everything is healthy (`docker ps`), then seed the test matrix once:

```bash
./scripts/seed.sh
```

### Test object matrix (`icinga2/test-objects.conf`)

All services are **passive**, so a pushed state sticks until changed.

| Service    | Seeded state               | Firmware should… |
| ---------- | -------------------------- | ---------------- |
| `svc-crit` | OK (trigger on demand)     | **fire** the siren when set CRITICAL |
| `svc-ack`  | CRITICAL + acknowledged    | **ignore** |
| `svc-dt`   | CRITICAL + in downtime     | **ignore** |
| `svc-flap` | CRITICAL + flapping        | **ignore** |
| `svc-ok`   | OK                         | never fires |

## 2. The JSON API the firmware uses

```bash
# Unhandled criticals (exactly what the firmware queries). limit=1 keeps it tiny.
curl -u admin:admin -H 'Accept: application/json' \
 'http://localhost:8080/icingadb/services?service.state.soft_state=2&service.state.is_acknowledged=n&service.state.in_downtime=n&service.state.is_flapping=n&limit=1'
```

Response is a top-level JSON array; each element has `name`, `display_name`,
`host.display_name` and a `state` object (`soft_state`, `next_check`,
`is_acknowledged`, `in_downtime`, `is_flapping`, …).

## 3. Run the virtual ESP32

```bash
docker-compose --profile sim up --build
```

It compiles `../trelaylaatern.ino` with the Arduino compatibility shim
(`esp32-sim/MockESP.h`) and polls `http://icingaweb2:8080/icingadb/...`.
Relay changes are printed to the log, e.g. `[GPIO] RELAY Pin 21 -> ON`.
Its web UI (same as the real device) is at http://localhost:8081 (`admin`/`admin`).

## 4. Scenarios

```bash
./scripts/set-critical.sh        # svc-crit CRITICAL  -> siren fires after `confirm_threshold` polls
./scripts/set-ok.sh              # svc-crit OK        -> siren clears
./scripts/ack.sh                 # svc-ack  CRITICAL+ack       (ignored)
./scripts/downtime.sh            # svc-dt   CRITICAL+downtime   (ignored)
./scripts/flap.sh                # svc-flap flapping           (ignored)
```

Watch the firmware react:

```bash
docker logs -f il-esp32-sim | grep -E 'checkIcinga|RELAY'
```

You should see the confirmation counter climb (`confirm=1/3`, `2/3`, `3/3`)
and the relay turn **ON only at `3/3`** — that is the debounce threshold.
While confirming, polls happen at the fast `recheck` cadence, not the slow one.

## Tear down

```bash
docker-compose --profile icinga --profile sim down            # keep data volumes
docker-compose --profile icinga --profile sim down -v         # wipe everything
```
