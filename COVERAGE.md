# Adapter Command Coverage

Availability of adapter commands across the two images. Compatibility matrices for the Go libraries live in [`go-mms/INTEROP.md`](https://github.com/otfabric/go-mms/blob/main/INTEROP.md) and [`go-iec61850/INTEROP.md`](https://github.com/otfabric/go-iec61850/blob/main/INTEROP.md).

| Adapter command | Image | Available |
|----------------|-------|:---------:|
| `libiec61850-mms-server` | `mms-interop-libiec61850` | ✓ |
| `libiec61850-mms-client` | `mms-interop-libiec61850` | ✓ |
| `libiec61850-ied-server` | `mms-interop-libiec61850` | ✓ |
| `libiec61850-ied-client` | `mms-interop-libiec61850` | ✓ |
| `libiec61850-ied-reporter` | `mms-interop-libiec61850` | ✓ |
| `iec61850bean-ied-server` | `mms-interop-iec61850bean` | ✓ |
| `iec61850bean-ied-client` | `mms-interop-iec61850bean` | ✓ |
| `iec61850bean-ied-reporter` | `mms-interop-iec61850bean` | — |

**Notes:**

- `iec61850bean-ied-reporter` is not yet implemented. It will be added when `go-iec61850` requires Bean URCB server-direction coverage.
- All adapter commands read from the fixture files in `fixtures/`.
- All commands emit JSON Lines to stdout; diagnostics go to stderr.
