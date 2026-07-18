# Implementation Plan

This document describes the implementation sequence for `mms-interop`. It reflects both the initial architecture and the design decisions made during review.

See [REQUIREMENTS.md](REQUIREMENTS.md) for the constraints that govern every step.

---

## Guiding principles

**Go libraries are the test drivers.** `go-mms` and `go-iec61850` are imported directly by the Go test files. Adapter processes are intentionally dumb: load a fixture, start a server or execute a fixed sequence, emit structured output, exit. All assertions live in Go.

**One adapter first.** The first release covers only the two libiec61850 MMS test directions. Adding a second adapter before those pass leaves four moving parts ambiguous on failure: fixture schema, adapter code, interop infrastructure, and target library behaviour.

**Resolve the largest technical uncertainty before building anything reusable.** The generic MMS server API in libiec61850 is the primary risk. Phase 0 answers that question before the fixture parser, Docker orchestration or Go test harness are written.

**Fixtures are separate from tests.** A fixture defines what a server exposes. A test defines what a client does and what it expects. These must not merge into a single document.

**Incremental protocol independence.** Each step adds one new source of diversity — a new direction, a new adapter, or a new protocol layer — only after the previous step is stable.

---

## Version pinning

All external dependencies must be pinned before Phase 0 begins. A change in an adapter must never silently change reference behaviour.

| Dependency | Pin |
|---|---|
| libiec61850 | `v1.6.1` (or most recent stable tag at the time of Phase 0) |
| iec61850bean | explicit release version, pinned in `adapters/iec61850bean/Dockerfile` |
| Docker base images | digest-pinned (e.g. `debian:bookworm-slim@sha256:...`) |
| go-mms | commit SHA of the branch under test |
| go-iec61850 | commit SHA of the branch under test |

libiec61850 is downloaded during the Docker build from a pinned release archive, with its source archive retained to satisfy GPLv3 corresponding-source obligations. It must not be copied into this repository.

CI must record the exact go-mms, go-iec61850 and adapter revisions used for every test run (see REQ-5.5).

---

## Go module selection

The repository tests work-in-progress builds of `go-mms` and `go-iec61850`, not published module versions.

**CI**: uses pinned commit SHAs via `go.mod` replace directives or a locked `go.work` file checked in for CI only.

**Local development**: an optional `go.work` file is supported but must not be committed:

```
// go.work — local only, not committed
go 1.23

use .
use ../go-mms
use ../go-iec61850
```

A Make target generates it:

```bash
make workspace GO_MMS=../go-mms GO_IEC61850=../go-iec61850
```

Do not commit machine-specific `replace ../go-mms` directives in `go.mod`.

---

## Adapter output contract

This contract applies to all adapter client commands. Server commands use the readiness event instead.

### Stdout — JSON Lines

Client adapters emit one JSON object per line to stdout. Each line is a complete, parseable result:

```jsonl
{"operation":"identify","ok":true,"value":{"vendor":"OTFabric","model":"MMS-Interop","revision":"1.0"}}
{"operation":"read","target":"interop/boolean","ok":true,"value":true}
{"operation":"read","target":"interop/integer","ok":true,"value":-123}
{"operation":"write","target":"interop/float32","ok":true}
{"operation":"conclude","ok":true}
```

### Stderr — human-readable diagnostics

All library log output, warnings and diagnostics go to stderr. Stdout must contain only JSON Lines. This contract is required because C and Java libraries commonly write to stdout by default.

### Readiness event

Server adapters emit one JSON object to stdout immediately after the listening socket is open and before accepting any connection:

```json
{"event":"ready","address":"0.0.0.0:102"}
```

Go tests wait for this line before dialling. Fixed startup sleeps are prohibited (see REQ-7.5).

### Exit codes

| Code | Meaning |
|---|---|
| 0 | Sequence completed; individual operation results determine test outcome |
| 1 | Adapter startup or configuration failure |
| 2 | Connection failure |
| 3 | Malformed or unreadable fixture |

### Test lifecycle

**Go tests own the full lifecycle.** A client test starts the adapter server, waits for the readiness event, dials, runs, and tears everything down. A server test starts the go-mms server, launches the adapter client, collects its stdout, and asserts all results. No external `docker compose up` command is required to run a test.

---

## Phase 0 — feasibility spike

**This phase must complete before any fixture parser, Docker orchestration or Go test harness is written.**

### Objective

Determine whether libiec61850's generic MMS server internals are accessible through a stable, public API that supports arbitrary domain and variable registration without constructing an IEC 61850 IedModel.

### Spike tasks

1. Pin a libiec61850 release (see version pinning above).
2. Build libiec61850 from source with the chosen pin.
3. Create one generic MMS domain programmatically.
4. Expose one integer variable in that domain.
5. Handle `Identify`, `GetNameList`, `Read` and `Write` for that variable.
6. Connect using `go-mms` and verify each operation succeeds.

### Questions the spike must answer

- Are all required MMS server headers public and stable across minor releases?
- Can arbitrary domains and variables be registered at runtime without an IedModel?
- Can read/write handlers be installed independently?
- Can `Identify` be configured with a custom vendor/model/revision?
- Can named variable-list creation and deletion be supported?
- Does the implementation rely only on installed headers or also on internal headers from the source tree?

Document the answers in `adapters/libiec61850/SPIKE.md` before proceeding.

### Fallbacks

If the generic MMS server API is not cleanly accessible, choose one of these fallbacks and document the choice explicitly:

**Fallback A** — Construct a minimal IEC 61850 model whose generated MMS domain and variable naming is fully known. Use it only for wire-level MMS tests with fixed, predictable names. This avoids any internal header dependency.

**Fallback B** — Maintain a small adapter-local patch over libiec61850's internal MMS APIs. Document the patched symbols, track upstream changes and accept the maintenance obligation.

**Fallback C** — Use libiec61850 only as a generic MMS client initially. Select a different implementation (for example, a minimal Go MMS server built from `go-mms` itself) for the generic MMS server role in Phase 1A.

Do not attempt all three in parallel. Choose the fallback with the lowest maintenance cost.

---

## Delivery sequence

No later step may be considered delivered until all preceding acceptance criteria pass. Exploratory groundwork — Docker build setup, SCL drafting, CI scaffolding, iec61850bean feasibility checks — may proceed in parallel with any phase.

---

### Phase 1A — first client direction

#### Step 1 — libiec61850 MMS server from fixture

Implement the `libiec61850-mms-server` entry point.

- Reads `fixtures/mms/interop.json`.
- Starts a generic MMS server using the API proven in Phase 0.
- Emits the readiness event to stdout when the socket is open.
- Serves declared domains, variables and types.
- Shuts down cleanly on SIGTERM or SIGINT.

Validates: fixture JSON is parseable by C; container starts; network is reachable.

#### Step 2 — go-mms client smoke test

Implement `tests/mms/go_mms_client_test.go`.

The Go test starts the adapter server, waits for the readiness event, then exercises the Phase 1A operation set:

| Operation | Detail |
|---|---|
| Associate | Negotiate PDU size, version, proposed services |
| Identify | Retrieve vendor, model, revision |
| GetNameList | Enumerate domains; enumerate variables within a domain |
| Read `boolean` | Read and assert value |
| Read `integer` | Read and assert value |
| Read `float32` | Read and assert value |
| Write `float32` | Write new value; read back and assert |
| Conclude | Graceful association release |
| Reconnect | Verify a new association succeeds after conclude |

Only these three scalar types are exercised here. Remaining types move to Phase 1C.

---

### Phase 1B — reverse direction

#### Step 3 — go-mms server fixture implementation

Implement a fixture-backed go-mms server that reads `fixtures/mms/interop.json` and serves the declared model. This server is started and stopped by the Go test, not by Docker Compose.

#### Step 4 — libiec61850 MMS client against go-mms server

Implement the `libiec61850-mms-client` entry point.

- Connects to the address supplied via environment variable.
- Executes the fixed Phase 1A operation sequence.
- Emits one JSON Line per operation to stdout.
- Exits with the appropriate exit code.

Implement `tests/mms/go_mms_server_test.go`. The test starts the go-mms server, launches the adapter client container, reads its stdout JSON Lines and asserts all results.

Validates: Go server association and response behaviour as seen by an independent C client.

---

**First release criterion:** Steps 1–4 pass. `go-mms` can associate, discover, read, write and disconnect successfully in both directions against libiec61850, using one shared deterministic fixture.

---

### Phase 1C — type and service expansion

#### Step 5 — Remaining scalar types

Extend both test files with the remaining fixture types:

`unsigned`, `visible-string`, `octet-string`, `bit-string`, `utc-time`

Also add `array` and `structure` reads in both directions.

See the [fixture encoding norms](#fixture-encoding-norms) section for the exact representation of each type.

#### Step 6 — Data-model operations

Extend both test files with:

| Operation | Detail |
|---|---|
| GetVariableAccessAttributes | Type introspection for scalars, arrays, structures |
| Multi-variable read | Read several variables in one request |
| Multi-variable write | Write several writable variables in one request |
| Named variable list | Define, read and delete |

#### Step 7 — Negative behaviour

Add negative test cases in both test files:

| Scenario | Assert |
|---|---|
| Read from unknown domain | MMS error class; no crash |
| Read unknown variable in known domain | MMS error class; no crash |
| Write with wrong type | MMS error class or access error |
| Write to read-only variable | MMS error class; value unchanged |
| Malformed or unsupported request | Service error or reject; no crash |

Assert the MMS error class or the broad outcome. Do not require byte-identical error responses unless the standard mandates a specific encoding.

---

### Phase 2 — iec61850bean MMS and IEC 61850

#### Step 8 — iec61850bean MMS directions

Implement `iec61850bean-mms-server` and `iec61850bean-mms-client` following the same contracts as the libiec61850 counterparts.

Run the full Phase 1A, 1B and 1C test suite against both iec61850bean MMS directions. Resolve any issues arising from the independent Java BER encoder, ACSE state machine and MMS state machine.

#### Step 9 — libiec61850 IEC 61850 directions

Implement `libiec61850-ied-server` and `libiec61850-ied-client`.

Implement `tests/iec61850/go_iec61850_client_test.go` and `tests/iec61850/go_iec61850_server_test.go` covering the Phase 2A operation set (see below).

#### Step 10 — iec61850bean IEC 61850 directions

Implement `iec61850bean-ied-server` and `iec61850bean-ied-client` and run the Phase 2A test suite against them.

#### Step 11 — Reports (Phase 2B)

Add unbuffered report testing in both directions:

- Enable one unbuffered RCB.
- Trigger a value change.
- Validate the received report: RptID, SqNum, inclusion bits, data references, values.

Add buffered report testing separately after unbuffered is stable.

#### Step 12 — Controls (Phase 2B)

Add direct-control testing in both directions:

- Operate one direct-control object.
- Verify command termination.

Add select-before-operate separately after direct control is stable.

---

## Phase definitions (summary)

### Phase 0 — feasibility

Spike proving libiec61850's generic MMS server API is usable. Must complete before Step 1.

### Phase 1A — first client direction (Steps 1–2)

go-mms client against libiec61850 MMS server. Smoke operations only (associate, identify, GetNameList, boolean/integer/float32 read/write, conclude/reconnect).

### Phase 1B — reverse direction (Steps 3–4)

libiec61850 MMS client against go-mms server. Same operation set. First release criterion.

### Phase 1C — type and service expansion (Steps 5–7)

Remaining scalar types, arrays, structures, data-model operations (GetVariableAccessAttributes, multi-variable, NVL) and negative behaviour. Applied in both directions.

### Phase 2A — go-iec61850, basic IEC discovery and access (Steps 9–10)

| Operation | Notes |
|---|---|
| Connect | Server identity |
| Discover logical devices | GetServerDirectory |
| Discover logical nodes | GetLogicalDeviceDirectory |
| Discover data objects | GetLogicalNodeDirectory |
| Read ST | Status functional constraint |
| Read MX | Measured values functional constraint |
| Read CF | Configuration functional constraint |
| Read DC | Description functional constraint |
| Write setpoint | One CF or SP attribute |
| Read predefined dataset | Members, values |

### Phase 2B — go-iec61850, reports and controls (Steps 11–12)

Reports (unbuffered before buffered) and controls (direct before SBO) are kept in a separate sub-phase. Each is substantially more stateful than Phase 2A and may consume comparable effort.

---

## Adapter design

Each adapter exposes four explicitly named commands. These commands may share a binary, library or package internally, but the MMS/IED and client/server roles must remain separately invocable and must not be selected through a generalised protocol test DSL.

```
adapters/libiec61850/src/
    mms_server.c    →  libiec61850-mms-server
    mms_client.c    →  libiec61850-mms-client
    ied_server.c    →  libiec61850-ied-server
    ied_client.c    →  libiec61850-ied-client

adapters/iec61850bean/src/
    MmsServer.java  →  iec61850bean-mms-server  (or launcher script)
    MmsClient.java  →  iec61850bean-mms-client
    IedServer.java  →  iec61850bean-ied-server
    IedClient.java  →  iec61850bean-ied-client
```

Each command should be small enough to compare directly against upstream library examples.

---

## Fixture design

### MMS fixture — `fixtures/mms/interop.json`

```json
{
  "identity": {
    "vendor": "OTFabric",
    "model": "MMS-Interop",
    "revision": "1.0"
  },
  "domains": {
    "interop": {
      "variables": {
        "boolean":        { "type": "boolean",        "value": true,                       "writable": true },
        "integer":        { "type": "integer",        "value": -123,                       "writable": true },
        "unsigned":       { "type": "unsigned",       "value": 456                                         },
        "float32":        { "type": "float32",        "value": 21.5,                       "writable": true },
        "visible_string": { "type": "visible-string", "value": "interop"                                   },
        "octet_string":   { "type": "octet-string",   "value": "DEADBEEF"                                  },
        "bit_string":     { "type": "bit-string",     "value": "10110"                                     },
        "utc_time":       { "type": "utc-time",       "value": "2024-01-01T00:00:00Z"                      },
        "array": {
          "type": "array",
          "elementType": "integer",
          "value": [1, 2, 3, 4, 5]
        },
        "structure": {
          "type": "structure",
          "value": [
            { "type": "boolean", "value": true },
            { "type": "integer", "value": 42   }
          ]
        }
      }
    }
  }
}
```

Rules:
- Declarative and static.
- No test expectations, callbacks, scripted transitions or per-adapter overrides.
- Additional variables are added only when a concrete test requires a type not already present.

### Fixture encoding norms

These norms are normative for all adapters. They must be documented in a short comment block inside each adapter's fixture parser.

**Octet strings** — case-insensitive hexadecimal, no `0x` prefix, even number of hex digits. `"DEADBEEF"` decodes to bytes `DE AD BE EF` (four bytes). Odd-length values are a fixture error.

**Bit strings** — MSB-first textual bit sequence. The number of meaningful bits equals the string length. `"10110"` is five bits; any padding bits required by the wire format are not part of the value. Adapters must preserve the exact bit count when encoding.

**UTC time** — ISO 8601 UTC timestamp (`Z` suffix required). Phase 1 treats quality fields (leapSecondsKnown, clockFailure, clockNotSynchronized, accuracy) as default/zero. Adapters must not assert specific quality bit values in Phase 1 tests.

**Integer** and **unsigned** — fixture values must fit comfortably in an interoperable signed or unsigned range (no values requiring more than 32 bits). Tests must not assert a specific BER byte length when multiple canonical widths are valid for the given value.

**Structure** — components are ordered and anonymous. `GetVariableAccessAttributes` is expected to return component type information in declaration order; tests must not assume named components.

### State reset between tests

A writable fixture creates shared mutable state. To avoid inter-test interference:

- Start a fresh server process or container for each top-level test case or test group, reloading the fixture from disk every time.
- Do not implement a fixture-reset endpoint or API.
- Do not rely on test-ordering or cleanup writes to restore initial values.

### IEC 61850 fixture — `fixtures/iec61850/`

`interop.icd` defines at minimum:

- One logical device (`InteropLD`).
- `LLN0` with `Mod` and `Beh`.
- `GGIO1` with `SPS1` (status) and `SPCSO1` (controllable single-point).
- `MMXU1` with `TotW` (measured active power).
- One predefined dataset.
- One unbuffered report control block.

`values.json` supplies initial mutable state:

```json
{
  "values": {
    "InteropLD/LLN0.Mod.stVal": 1,
    "InteropLD/GGIO1.SPS1.stVal": false,
    "InteropLD/MMXU1.TotW.mag.f": 1234.5
  },
  "writable": [
    "InteropLD/GGIO1.SPCSO1"
  ]
}
```

---

## Packet capture

Run `tcpdump` in the Docker network on failure (and optionally on every CI run). Retain captures as build artifacts:

```
artifacts/
    libiec61850-go-mms-client.pcap
    libiec61850-go-mms-server.pcap
    iec61850bean-go-mms-client.pcap
    iec61850bean-go-mms-server.pcap
    libiec61850-go-iec61850-client.pcap
    libiec61850-go-iec61850-server.pcap
```

PCAP files are not used as golden-file test oracles. Use them for post-failure diagnosis of:

- Association negotiation differences.
- Presentation-context problems.
- BER encoding mismatches.
- COTP/session disconnects.
- Report encoding issues.

---

## What is not in this plan

The following topics are out of scope. They may be revisited by an explicit separate decision.

| Topic | Reason deferred |
|---|---|
| GOOSE, Sampled Values | Different transport; different test infrastructure |
| IEDExplorer | Manual spot-check only; limited Linux support |
| Triangle MicroWorks | Commercial; useful later for conformance-oriented validation |
| Buffered reports before unbuffered | Too stateful; deferred within Phase 2B |
| SBO controls before direct | Too stateful; deferred within Phase 2B |
| JSON Schema for fixture | Add if malformed fixtures become a real maintenance problem |
| Python bindings | Same libiec61850 stack; no protocol diversity |
| Metadata overlays, profiles, capability matrices | Overhead without demonstrated need |
