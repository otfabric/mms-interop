# Adapter Command Coverage

Availability of adapter commands across the two images. Compatibility matrices for the Go libraries live in [`go-mms/INTEROP.md`](https://github.com/otfabric/go-mms/blob/main/INTEROP.md) and [`go-iec61850/INTEROP.md`](https://github.com/otfabric/go-iec61850/blob/main/INTEROP.md).

This file is generated from `fixtures/capabilities.json`; do not edit manually.

| Adapter command | Image | Available |
|----------------|-------|:---------:|
| `libiec61850-mms-server` | `mms-interop-libiec61850` | ✓ |
| `libiec61850-mms-client` | `mms-interop-libiec61850` | ✓ |
| `libiec61850-ied-server` | `mms-interop-libiec61850` | ✓ |
| `libiec61850-ied-controller` | `mms-interop-libiec61850` | ✓ |
| `libiec61850-ied-reporter` | `mms-interop-libiec61850` | ✓ |
| `iec61850bean-ied-server` | `mms-interop-iec61850bean` | ✓ |
| `iec61850bean-ied-controller` | `mms-interop-iec61850bean` | ✓ |
| `iec61850bean-ied-reporter` | `mms-interop-iec61850bean` | ✓ |

**Known limitations (verified upstream gaps):**

| Stack | Version | Direction | Capability | Expected skip |
|-------|---------|-----------|------------|---------------|
| `iec61850bean` | 1.x | go-client → iec61850bean-server | SBOw `SelectWithValue` | `TestBeanClient_Control_SBOwOperate` |

**Notes:**

- All adapter commands read from the fixture files in `fixtures/`.
- All commands emit JSON Lines to stdout; diagnostics go to stderr.
- Each adapter command supports `--capabilities` (emit JSON and exit) and `--version` (emit version JSON and exit).
- A skipped test without a registered limitation in this table must fail CI.
