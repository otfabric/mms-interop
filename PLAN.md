# mms-interop — Plan

---

## 1. Scope and principles

### Purpose

`mms-interop` is **interoperability infrastructure**, not the primary test owner for the Go libraries.

It provides:
- Independent adapter implementations (libiec61850, iec61850bean) in reproducible Docker images.
- Deterministic fixture files (`interop.json`, `interop.icd`, `values.json`) that define the canonical model and values.
- The JSON Lines adapter contract that all consumers depend on.
- Published adapter images versioned at `ghcr.io/otfabric/`.

**`go-mms` and `go-iec61850` own:**
- Which public API calls are exercised.
- Expected library behaviour and error semantics.
- All assertions.
- Release gating.
- Regression diagnosis and compatibility claims for their own implementation.
- Compatibility matrices and feature roadmaps.

**`mms-interop` owns:**
- The independent adapter implementations.
- Fixture models and runtime values.
- Docker packaging and adapter image publication.
- The adapter startup and command contracts.
- Fixed external-stack behaviour and versioning.
- Adapter self-tests (image starts, readiness output is valid, JSON Lines output is syntactically correct, required binaries are present).

### Repository relationship

```
               mms-interop
          containers + fixtures
               /           \
              /             \
         go-mms           go-iec61850
      interop/             interop/
   owns MMS tests      owns IEC 61850 tests
```

Consumer repositories pin a specific adapter image. They start containers, wait for the readiness event, exercise the Go library under test, and assert results — all within their own `interop/` package (behind `-tags=interop`).

This mirrors the `go-s7comm` / `snap7-interop` pattern.

### Adapter evolution policy

Adapter infrastructure is extended only when a consumer repository adds a concrete interoperability requirement that cannot be satisfied by the existing adapter commands or fixtures. Examples:

- A new adapter command (e.g. `libiec61850-ied-reporter`) required when `go-iec61850` needed to test URCB server-direction reporting.
- A new fixture attribute required when `go-mms` adds a type that is not yet in `interop.json`.
- A new dataset member required when `go-iec61850` tests multi-member reports.

`mms-interop` does not independently decide which Go library feature to cover next. That decision belongs to each Go repository's own roadmap.

### Constraints

- **Adapters are intentionally dumb.** Load a fixture, start a server or execute a fixed sequence, emit structured output, exit. No test logic lives in adapter code.
- **Fixtures and assertions are separate.** A fixture defines what a server exposes. Test assertions live in the consuming Go repository.
- **One meaningful source of protocol diversity per phase.** Each adapter capability addition introduces one new direction or protocol layer.
- **Assertions describe protocol semantics.** Assert error classes or broad outcomes, not byte-identical wire responses.

### Architectural decisions

**Generic MMS interoperability is tested only against libiec61850.**
`iec61850bean` is an IEC 61850 stack, not a useful second generic MMS implementation. Its MMS layer is not exposed through a practical generic client/server API. Generic MMS coverage is sufficient from the two libiec61850 directions.

**iec61850bean is used at the IEC 61850 semantic layer only.**
It provides two commands: `iec61850bean-ied-server` and `iec61850bean-ied-client`.

### Adapter output contract

**Stdout — JSON Lines.** Client adapters emit one JSON object per line. Each line is a complete, parseable result:

```jsonl
{"operation":"read","target":"InteropLD/GGIO1.SPS1.stVal[ST]","ok":true,"value":false}
{"operation":"write","target":"InteropLD/LLN0.Mod.stVal[ST]","ok":true}
{"operation":"conclude","ok":true}
```

**Stderr — diagnostics only.** All library log output goes to stderr. Stdout must contain only JSON Lines.

**Readiness event.** Server adapters emit one JSON object to stdout immediately after the listening socket is open:

```json
{"event":"ready","address":"0.0.0.0:1102","fixture":"mms-v1","adapter":"libiec61850","version":"0.1.0"}
```

`fixture` identifies the canonical fixture revision consumed (e.g. `mms-v1`, `iec61850-v1`). `version` is the adapter image version from the `ADAPTER_VERSION` environment variable, defaulting to `dev`.

Go tests wait for this line before dialling. Fixed startup sleeps are prohibited.

**Exit codes:**

| Code | Meaning |
|------|---------|
| 0 | Sequence completed; individual operation results determine outcome |
| 1 | Adapter startup or configuration failure |
| 2 | Connection failure |
| 3 | Malformed or unreadable fixture |

### Version pinning

All external adapter dependencies are pinned. A change in an adapter must never silently change reference behaviour.

| Dependency | Pin |
|------------|-----|
| libiec61850 | commit SHA pinned in `adapters/libiec61850/Dockerfile` (`libIEC61850_SHA`) |
| iec61850bean | `com.beanit:iec61850bean:1.9.0` pinned in `adapters/iec61850bean/pom.xml` |
| Docker base images | digest-pinned (`@sha256:...`) in each Dockerfile |

mms-interop CI records:
- mms-interop SHA
- libiec61850 SHA / tag
- iec61850bean version
- adapter image digest
- target platform

---

## 2. Delivered adapter infrastructure

### Phase 0 — feasibility spike

Confirmed that libiec61850's internal MMS server API is accessible through private headers. Findings documented in `adapters/libiec61850/SPIKE.md`.

### Delivered adapters

| Image | Commands |
|-------|----------|
| `mms-interop-libiec61850` | `libiec61850-mms-server`, `libiec61850-mms-client`, `libiec61850-ied-server`, `libiec61850-ied-client`, `libiec61850-ied-reporter` |
| `mms-interop-iec61850bean` | `iec61850bean-ied-server`, `iec61850bean-ied-client` |

### Delivered fixtures

| File | Description |
|------|-------------|
| `fixtures/mms/interop.json` | MMS domain, variable types, initial values |
| `fixtures/iec61850/interop.icd` | SCL model (LDs, LNs, DOs, dataset, URCB) |
| `fixtures/iec61850/values.json` | Runtime values for the IEC 61850 model |

### Consumer coverage

Consumers currently exercise both MMS directions and four IEC 61850 directions across the adapter commands above. The compatibility matrices and feature roadmaps are maintained in `go-mms/INTEROP.md` and `go-iec61850/INTEROP.md`.

See [COVERAGE.md](COVERAGE.md) for adapter command availability.

---

## 3. Current adapter gaps

| Gap | Status |
|-----|--------|
| `iec61850bean-ied-reporter` | Not implemented; needed when `go-iec61850` adds Bean URCB server direction |
| Multi-URCB fixture members | Single dataset member; extend when consumer requests multi-member report coverage |
| Direct-control command | No `Oper` adapter yet; needed when `go-iec61850` adds control coverage |
| Dynamic dataset commands | Not planned until consumer demonstrates the need |

---

## What is not in this plan

| Topic | Reason |
|-------|--------|
| Go library compatibility matrices | Owned by `go-mms/INTEROP.md` and `go-iec61850/INTEROP.md` |
| Go library feature roadmaps | Owned by each Go repository |
| Go test naming conventions | Documented in each `interop/README.md` |
| GOOSE, Sampled Values | Different transport; different test infrastructure |
| Broad vendor compatibility matrix | Overhead without demonstrated need |
| Conformance certification | Later stage |
| Metadata overlays, generalized adapter DSL | Overhead without demonstrated need |
