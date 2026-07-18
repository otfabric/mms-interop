# mms-interop

Interoperability **infrastructure** for [go-mms](https://github.com/otfabric/go-mms) and [go-iec61850](https://github.com/otfabric/go-iec61850).

This repository provides adapter images, fixture files and the adapter output contract. It does not own assertions about the Go libraries. Interoperability tests and compatibility matrices live in the Go repositories and consume the infrastructure here.

```
               mms-interop
          containers + fixtures
               /           \
              /             \
         go-mms           go-iec61850
      interop/             interop/
   owns MMS tests      owns IEC 61850 tests
```

## Purpose

- Publish reproducible adapter images for libiec61850 and iec61850bean.
- Define the canonical fixture models and runtime values that all interop tests rely on.
- Specify the JSON Lines adapter output contract that adapters must implement.
- Keep the adapter implementations simple and auditable against upstream library examples.

## Repository structure

```
mms-interop/
├── README.md
├── PLAN.md
├── COVERAGE.md               # Adapter command availability
├── Makefile
├── docker-compose.yml
│
├── fixtures/
│   ├── mms/
│   │   └── interop.json      # MMS server model and initial variable state
│   └── iec61850/
│       ├── interop.icd       # SCL model (logical devices, nodes, data objects)
│       └── values.json       # Runtime values and writable attribute list
│
└── adapters/
    ├── libiec61850/
    │   ├── Dockerfile
    │   ├── SPIKE.md           # Phase 0 findings
    │   └── src/
    └── iec61850bean/
        ├── Dockerfile
        └── src/
```

## Library boundary

| Library | Responsibility |
|---|---|
| `go-mms` | ISO session / presentation / ACSE, MMS association, BER encoding, generic MMS services, named variable lists |
| `go-iec61850` | IEC 61850 object references, functional constraints, logical-node discovery, datasets, reports, controls, SCL handling — uses `go-mms` underneath |

`go-mms` must never accumulate IEC 61850 semantics, even as a convenience.

## Adapters

Third-party implementations run as separate Docker containers. They are intentionally simple: load a fixture, start a server or execute a fixed operation sequence, emit structured JSON Lines, exit.

### libiec61850

[libIEC61850](https://libiec61850.com) is the primary reference. It exposes a distinct low-level generic MMS API and a separate high-level IEC 61850 API, which maps cleanly onto the `go-mms` / `go-iec61850` split.

libiec61850 is built and executed as a separate GPLv3-licensed containerised program and is not linked into the MIT-licensed Go libraries. It is downloaded from a pinned release archive during the Docker build. Distribution of any image containing it must preserve the applicable GPLv3 notices and corresponding source obligations.

Commands:

- `libiec61850-mms-server` — generic MMS server loaded from `fixtures/mms/interop.json`
- `libiec61850-mms-client` — generic MMS client, executes a fixed sequence and emits JSON Lines
- `libiec61850-ied-server` — IEC 61850 IED server loaded from `fixtures/iec61850/`
- `libiec61850-ied-client` — IEC 61850 client, executes a fixed sequence and emits JSON Lines
- `libiec61850-ied-reporter` — IEC 61850 client that subscribes to URCB and emits report fields as JSON Lines

### iec61850bean

[IEC61850bean](https://www.beanit.com/iec-61850/) (Apache-2.0) is the secondary reference. It provides an independently implemented ASN.1/BER encoder, ACSE state machine, MMS protocol handler and IEC 61850 mapping — genuine protocol diversity rather than a language wrapper over the same core.

`iec61850bean` is used at the IEC 61850 semantic layer only. Its MMS layer is not exposed through a practical generic MMS API, so generic MMS interoperability is covered by the libiec61850 directions.

Commands:

- `iec61850bean-ied-server` — IEC 61850 IED server loaded from `fixtures/iec61850/`
- `iec61850bean-ied-client` — IEC 61850 client, executes a fixed sequence and emits JSON Lines

## Fixtures

### MMS — `fixtures/mms/interop.json`

Defines domains, variables, types and initial values. Consumed by the libiec61850 C adapter. Consumer repositories (`go-mms`) carry a synchronized copy in `interop/testdata/`.

Initial type baseline: `boolean`, `integer`, `unsigned`, `float32`, `visible-string`, `octet-string`, `bit-string`, `utc-time`, `array`, `structure`.

New types are added only when `go-mms` claims support for them or when they represent a known interoperability risk.

### IEC 61850 — `fixtures/iec61850/`

`interop.icd` is the canonical SCL model file (logical devices, logical nodes, data objects, datasets, report control blocks). `values.json` supplies mutable runtime state that SCL cannot express.

Consumer repositories (`go-iec61850`) carry synchronized copies in `interop/testdata/`. The adapter image version and fixture revision must remain compatible — they are updated together.

## Consuming the adapter images

Consumer repositories pin a specific adapter image version tag for local use and a digest for CI:

```bash
# Local development — version tag
LIBIEC61850_IMAGE=ghcr.io/otfabric/mms-interop-libiec61850:v0.1.0 make interop

# CI — digest-pinned for full reproducibility
LIBIEC61850_IMAGE=ghcr.io/otfabric/mms-interop-libiec61850@sha256:<digest> make interop
```

Each test lifecycle:

1. Starts the adapter container with `docker run`.
2. Waits for the readiness event on stdout (`{"event":"ready",...}`).
3. Exercises the Go library under test.
4. Collects adapter stdout (JSON Lines) and asserts results.
5. Stops the container.

No pre-running containers are required. Tests run with `-tags=interop` and `make interop` in each Go repository.

## Adapter output contract

Server readiness event (emitted to stdout before accepting connections):

```json
{"event":"ready","address":"0.0.0.0:102","fixture":"iec61850-v1","adapter":"libiec61850","version":"0.1.0"}
```

`fixture` identifies the canonical fixture revision consumed. `version` is the adapter image version, set via the `ADAPTER_VERSION` build argument and defaulting to `dev`.

Client adapter output (one JSON Line per operation):

```jsonl
{"operation":"identify","ok":true,"value":{"vendor":"OTFabric","model":"MMS-Interop","revision":"1.0"}}
{"operation":"read","target":"interop/boolean","ok":true,"value":true}
{"operation":"write","target":"interop/float32","ok":true}
{"operation":"conclude","ok":true}
```

Diagnostics go to stderr. Stdout must contain only JSON Lines.

## Test directions

Six implemented directions (tests live in `go-mms` and `go-iec61850`):

| Direction | Layer | Go role | Adapter | Phase |
|---|---|---|---|---|
| go-mms client → libiec61850 MMS server | MMS | Client | libiec61850 | 1A |
| libiec61850 MMS client → go-mms server | MMS | Server | libiec61850 | 1B |
| go-iec61850 client → libiec61850 IED server | IEC 61850 | Client | libiec61850 | 2A |
| libiec61850 IED client → go-iec61850 server | IEC 61850 | Server | libiec61850 | 2A |
| go-iec61850 client → iec61850bean IED server | IEC 61850 | Client | iec61850bean | 2B |
| iec61850bean IED client → go-iec61850 server | IEC 61850 | Server | iec61850bean | 2B |

## Building and publishing adapter images

```bash
# Build both adapter images locally
make build

# Publish to registry (requires REGISTRY and VERSION or defaults)
make publish
```

Docker Compose is provided for manual inspection:

```bash
docker compose up libiec61850-mms-server
docker compose down
```

## Running interop tests

Interop tests live in the Go repositories. To run them against locally built images:

```bash
# In go-mms
LIBIEC61850_IMAGE=mms-interop-libiec61850:local make interop

# In go-iec61850
LIBIEC61850_IMAGE=mms-interop-libiec61850:local \
IEC61850BEAN_IMAGE=mms-interop-iec61850bean:local \
make interop
```

## Prerequisites

- Docker with BuildKit / buildx
- Make

## License

The repository's original code and documentation are MIT-licensed unless otherwise noted.

Third-party source code, Docker images and generated artifacts retain their respective upstream licenses. libiec61850 is GPLv3; iec61850bean is Apache-2.0. Distribution of images containing either library must preserve the applicable upstream notices and source obligations.
