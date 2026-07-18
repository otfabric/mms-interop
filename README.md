# mms-interop

Interoperability test suite for [go-mms](https://github.com/otfabric/go-mms) and [go-iec61850](https://github.com/otfabric/go-iec61850).

The repository validates both libraries against independent, established implementations. It is not a general compatibility framework or a conformance certification harness.

## Purpose

- Prove that `go-mms` correctly speaks generic ISO 9506 MMS over the full ISO transport stack in both client and server roles.
- Prove that `go-iec61850` correctly maps IEC 61850 ACSI behaviour onto MMS in both client and server roles.
- Keep diagnostic ownership clear: MMS encoding failures surface in `go-mms` tests; IEC 61850 object-model failures surface in `go-iec61850` tests.

## Repository structure

```
mms-interop/
├── README.md
├── REQUIREMENTS.md
├── PLAN.md
├── docker-compose.yml
├── Makefile
│
├── fixtures/
│   ├── mms/
│   │   └── interop.json          # MMS server model and initial variable state
│   └── iec61850/
│       ├── interop.icd           # SCL model (logical devices, nodes, data objects)
│       └── values.json           # Runtime values and writable attribute list
│
├── adapters/
│   ├── libiec61850/
│   │   ├── Dockerfile
│   │   ├── SPIKE.md              # Phase 0 findings and API notes
│   │   └── src/
│   └── iec61850bean/
│       ├── Dockerfile
│       └── src/
│
└── tests/
    ├── mms/                      # Go tests using go-mms directly
    └── iec61850/                 # Go tests using go-iec61850 directly
```

## Library boundary

The library split is intentional and must be preserved.

| Library | Responsibility |
|---|---|
| `go-mms` | ISO session / presentation / ACSE, MMS association, BER encoding, generic MMS services, named variable lists |
| `go-iec61850` | IEC 61850 object references, functional constraints, logical node discovery, datasets, reports, controls, SCL handling — uses `go-mms` underneath |

`go-mms` must never accumulate IEC 61850 semantics, even as a convenience.

## Adapters

Third-party implementations run as separate Docker containers. They are intentionally simple: load a fixture, start a server or execute a fixed operation sequence, emit structured JSON Lines, exit. All assertions live in the Go test files.

### libiec61850

[libIEC61850](https://libiec61850.com) is the primary reference. It exposes a distinct low-level generic MMS API and a separate high-level IEC 61850 API, which maps cleanly onto the `go-mms` / `go-iec61850` split.

libiec61850 is built and executed as a separate GPLv3-licensed containerised program and is not linked into the MIT-licensed Go libraries. It is downloaded from a pinned release archive during the Docker build; it is not copied into this repository. Distribution of any image containing it must preserve the applicable GPLv3 notices and corresponding source obligations.

Four commands, one per role:

- `libiec61850-mms-server` — generic MMS server loaded from `fixtures/mms/interop.json`
- `libiec61850-mms-client` — generic MMS client, executes a fixed sequence and emits JSON Lines
- `libiec61850-ied-server` — IEC 61850 IED server loaded from `fixtures/iec61850/`
- `libiec61850-ied-client` — IEC 61850 client, executes a fixed sequence and emits JSON Lines

### iec61850bean

[IEC61850bean](https://www.beanit.com/iec-61850/) (formerly OpenIEC61850, Apache-2.0) is the secondary reference. It provides an independently implemented ASN.1/BER encoder, ACSE state machine, MMS protocol handler and IEC 61850 mapping — genuine protocol diversity rather than a language wrapper over the same core.

Four commands, mirroring libiec61850:

- `iec61850bean-mms-server`
- `iec61850bean-mms-client`
- `iec61850bean-ied-server`
- `iec61850bean-ied-client`

## Fixtures

### MMS — `fixtures/mms/interop.json`

Defines domains, variables, types and initial values. A single static JSON document consumed by Go, Java and C adapters. It does not encode test expectations, callbacks or per-adapter overrides.

Initial type baseline: `boolean`, `integer`, `unsigned`, `float32`, `visible-string`, `octet-string`, `bit-string`, `utc-time`, `array`, `structure`.

Additional types are added only when `go-mms` claims support for them or when they represent a known interoperability risk.

### IEC 61850 — `fixtures/iec61850/`

`interop.icd` is the SCL model file (logical devices, logical nodes, data objects, datasets, report control blocks). `values.json` supplies mutable runtime state that SCL cannot express. Using SCL as the primary format avoids building a proprietary IEC 61850 schema.

## Test structure

The Go libraries are the test drivers. Adapter processes emit JSON Lines to stdout; Go tests collect and assert them. Go tests own the full lifecycle: they start adapter processes, wait for the readiness event, dial, run assertions, and tear down. No pre-running containers are required to execute a test.

```
tests/mms/
    go_mms_client_test.go    # go-mms as client, adapter as server
    go_mms_server_test.go    # go-mms as server, adapter as client

tests/iec61850/
    go_iec61850_client_test.go
    go_iec61850_server_test.go
```

Adapter client output (one JSON Line per operation):

```jsonl
{"operation":"identify","ok":true,"value":{"vendor":"OTFabric","model":"MMS-Interop","revision":"1.0"}}
{"operation":"read","target":"interop/boolean","ok":true,"value":true}
{"operation":"write","target":"interop/float32","ok":true}
{"operation":"conclude","ok":true}
```

Server readiness event (one line emitted before accepting connections):

```json
{"event":"ready","address":"0.0.0.0:102"}
```

## Packet capture

On test failure (and optionally on every CI run), `tcpdump` captures are retained as debugging artifacts:

```
artifacts/
    libiec61850-go-mms-client.pcap
    libiec61850-go-mms-server.pcap
```

PCAP files are not used as golden-file test oracles. They exist to diagnose association negotiation, BER encoding, COTP/session layer and report encoding issues.

## Test directions

Eight eventual test directions, implemented incrementally (see [PLAN.md](PLAN.md)):

| Direction | Layer | Go role | Adapter |
|---|---|---|---|
| go-mms client → libiec61850 MMS server | MMS | Client | libiec61850 |
| libiec61850 MMS client → go-mms server | MMS | Server | libiec61850 |
| go-mms client → iec61850bean MMS server | MMS | Client | iec61850bean |
| iec61850bean client → go-mms server | MMS | Server | iec61850bean |
| go-iec61850 client → libiec61850 IED server | IEC 61850 | Client | libiec61850 |
| libiec61850 client → go-iec61850 server | IEC 61850 | Server | libiec61850 |
| go-iec61850 client → iec61850bean IED server | IEC 61850 | Client | iec61850bean |
| iec61850bean client → go-iec61850 server | IEC 61850 | Server | iec61850bean |

The first release covers only the two libiec61850 MMS directions (Steps 1–4 in PLAN.md).

## Out of scope

The following are explicitly excluded from the initial implementation:

- GOOSE and Sampled Values
- Broad vendor compatibility matrix
- Complex metadata overlays or per-adapter fixture variants
- Generalised test orchestration framework
- Conformance certification
- Journals and file transfer (deferred)
- Buffered reports, select-before-operate and dynamic datasets (Phase 2B)
- Triangle MicroWorks and other commercial tools (later conformance stage)
- IEDExplorer (manual spot-check only; limited Linux support)
- Python wrappers around libiec61850 (same protocol stack, no diversity)

## Prerequisites

- Docker
- Go compatible with the `go` directive in `go.mod` (currently Go 1.23+)
- Make

## Configuration

Adapter server address and port are configurable through environment variables:

| Variable | Default | Description |
|---|---|---|
| `MMS_INTEROP_ADDRESS` | `127.0.0.1` | Address Go tests dial when connecting to adapter servers |
| `MMS_INTEROP_PORT` | `1102` | Host-side mapped port (container port 102 mapped to this) |

Hardcoded `localhost:102` references are prohibited in test code.

## Local development

To test against a local working tree of `go-mms` or `go-iec61850`, generate a `go.work` file:

```bash
make workspace GO_MMS=../go-mms GO_IEC61850=../go-iec61850
```

The generated `go.work` file is gitignored. Do not commit it.

## Quick start

Each Go test manages the full adapter lifecycle — start, readiness, run, teardown. Tests are a single command:

```bash
# Build adapter images (once)
make build

# Run go-mms client tests (starts libiec61850 MMS server internally)
go test ./tests/mms/... -run Client -v

# Run go-mms server tests (starts go-mms server, launches libiec61850 MMS client internally)
go test ./tests/mms/... -run Server -v
```

Docker Compose is provided for manual debugging:

```bash
# Start a server for manual inspection
docker compose up libiec61850-mms-server

# Tear down
docker compose down
```

## License

The repository's original code and documentation are MIT-licensed unless otherwise noted. See [LICENSE](LICENSE).

Third-party source code, Docker images and generated artifacts retain their respective upstream licenses. libiec61850 is GPLv3; iec61850bean is Apache-2.0. Distribution of images containing either library must preserve the applicable upstream notices and source obligations.
