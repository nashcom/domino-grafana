# DomProbe

Domino NRPC blackbox exporter — a lightweight Prometheus-compatible probe for checking HCL Domino server availability via NRPC.
Runs as a native Domino servertask on Linux.

---

## Overview

DomProbe listens on an HTTP port and handles probe requests from Prometheus or any HTTP client.
For each request it connects to the target Domino server via NRPC, measures latency, optionally opens a database, and returns the results in Prometheus text format.
It is intentionally a **availability probe only**. Deeper statistics (cluster replication, mail routing, sessions, memory) are out of scope — use DomProm or node_exporter for those.

## Building

The project provides a Windows and Linux makefile.


## Running

### As a Domino servertask (Linux)

Copy `domprobe` to the Domino program directory and add to `ServerTasks` in notes.ini, or load manually:

```
load domprobe
tell domprobe status
tell domprobe quit
```


## Configuration (notes.ini)

| Parameter                   | Default | Description                                       |
|-----------------------------|---------|---------------------------------------------------|
| `DOMPROBE_ListenPort`       | `9115`  | HTTP listener port (Prometheus Blackbox standard) |
| `DOMPROBE_Workers`          | `10`    | Worker thread count (1–100)                       |
| `DOMPROBE_QueueSize`        | `1000`  | Request queue depth (100–10000)                   |
| `DOMPROBE_Debug`            | `0`     | Debug logging: 0=off, 1=on                        |
| `DOMPROBE_AllowConsole`     | `0`     | Console access: `0`=disabled `1`=loopback only `2`=all addresses (requires `DOMPROBE_ConsoleAuthToken`) |
| `DOMPROBE_AuthToken`        | `-`     | Bearer token for `/probe`. If set, all probe requests require auth.   |
| `DOMPROBE_ConsoleAuthToken` | `-`     | Bearer token for `/console`. Always required when console is enabled. |

## Endpoints

### `GET /health`

Returns `200 OK` unconditionally. Use for load balancer or liveness checks.

### `GET /metrics`

Returns DomProbe's own operational counters in Prometheus format.

```
domino_probe_probes_total 42
domino_probe_probe_errors_total 3
domino_probe_probe_duration_seconds_total 8.371
domino_probe_console_requests_total 5
domino_probe_console_errors_total 0
domino_probe_queue_rejected_total 0
domino_probe_start_timestamp_seconds 1747123456
domino_probe_uptime_seconds 3602
```

### `GET /probe`

Probes a Domino server via NRPC. Returns Prometheus metrics.

| Parameter | Required | Description |
|-----------|----------|-------------|
| `target`  | yes      | Domino server hostname or CN to probe |
| `db`      | no       | Database path to open after a successful ping (e.g. `names.nsf`). Adds `domino_probe_database_open` and `domino_probe_database_open_seconds` to the response. |
| `auth`    | no       | Authentication token. Required if `DOMPROBE_AuthToken` is set. Alternative to `Authorization: Bearer` header. |

```
GET /probe?target=domino01.acme.com
GET /probe?target=domino01.acme.com&db=names.nsf
GET /probe?target=domino01.acme.com&db=names.nsf&auth=mysecrettoken
```

### `GET /console`

Executes a remote console command on the target server and returns the output as plain text.

Requires `DOMPROBE_AllowConsole=1` or `2` and `DOMPROBE_ConsoleAuthToken` to be set.

| Parameter | Required | Description |
|-----------|----------|-------------|
| `target`  | yes      | Domino server hostname or CN to run the command on |
| `cmd`     | yes      | Console command to execute. URL-encoded; use `+` for spaces. Allowed verbs: `show`, `tell`, `drop`, `load`, `restart`, `set`. |
| `auth`    | yes      | Authentication token matching `DOMPROBE_ConsoleAuthToken`. Alternative to `Authorization: Bearer` header. |

```
GET /console?target=domino01.acme.com&cmd=show+server&auth=mysecrettoken
GET /console?target=domino01.acme.com&cmd=show+tasks&auth=mysecrettoken
GET /console?target=domino01.acme.com&cmd=tell+router+show&auth=mysecrettoken
```

### `GET /quit` *(test mode only)*

Triggers graceful shutdown. Only available when `KitType=1` (Notes client).


## Authentication

`/probe` and `/console` use independent tokens. Set one, both, or neither.

If `DOMPROBE_AuthToken` is set, the token must be provided on every `/probe` request.
If `DOMPROBE_ConsoleAuthToken` is set (required for console), the token must be provided on every `/console` request.

**URL parameter:**
```
GET /probe?target=domino01.acme.com&auth=mysecrettoken
```

**HTTP header:**
```
Authorization: Bearer my-secret-token
```

`/health` and `/metrics` are always open.


## Probe Metrics

### NRPC probe (always returned)

| Metric                                  | Type  | Description                 |
|-----------------------------------------|-------|-----------------------------|
| `domino_probe_success`                  | gauge | 1 if NSPingServer succeeded |
| `domino_probe_server_state`             | gauge | 0=available 1=restricted 2=unavailable 3=not_reachable |
| `domino_probe_availability_index`       | gauge | Responding server count from NSPingServer              |
| `domino_probe_client_to_server_seconds` | gauge | NRPC request latency  |
| `domino_probe_server_to_client_seconds` | gauge | NRPC reply latency    |
| `domino_probe_duration_seconds`         | gauge | Total probe duration  |
| `domino_probe_error_code`               | gauge | Domino error code (HELP line contains error text) — only on error |

### Database probe (`db=` parameter)

| Metric                               | Type  | Description                          |
|--------------------------------------|-------|--------------------------------------|
| `domino_probe_database_open`         | gauge | 1 if NSFDbOpen succeeded             |
| `domino_probe_database_open_seconds` | gauge | Time to open the database            |


## Server State Values

| Value | Meaning   |
|-------|-----------|
| `0`   | Available |
| `1`   | Restricted (server is up but access restricted) |
| `2`   | Unavailable (server busy)   |
| `3`   | Not reachable (likely down) |


## Prometheus Scrape Config

```yaml
scrape_configs:
  - job_name: domino_probe
    metrics_path: /probe
    params:
      db: [names.nsf]
    static_configs:
      - targets:
          - domino01.acme.com
          - domino02.acme.com
    relabel_configs:
      - source_labels: [__address__]
        target_label: __param_target
      - source_labels: [__param_target]
        target_label: instance
      - target_label: __address__
        replacement: domprobe-host:9115
```


## Servertask Commands

```
tell domprobe help     — show available commands
tell domprobe status   — show current configuration
tell domprobe quit     — graceful shutdown
```
