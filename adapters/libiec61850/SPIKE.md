# Phase 0 — libIEC61850 Generic MMS Server Feasibility Spike

## Questions to answer

1. Can the libIEC61850 MMS server serve an arbitrary domain with arbitrary variable names (not IEC 61850 FC paths)?
2. Which headers are required and are they in the public install or only in the source tree?
3. Do `MmsServer_installReadHandler` / `MmsServer_installWriteHandler` behave as expected?
4. Can the server emit a JSON readiness signal before accepting connections?

## Findings (libIEC61850 v1.6.1)

### Header availability

| Header | Location | Installed? | Notes |
|--------|----------|-----------|-------|
| `mms_server.h` | `src/mms/inc/` | ✓ yes | Contains `MmsServer` opaque type and `LIB61850_INTERNAL` API stubs. `MmsServer_setServerIdentity` is accessible here. |
| `mms_types.h` | `src/mms/inc/` | ✓ yes | Contains the full `struct sMmsVariableSpecification` definition. |
| `mms_value.h` | `src/mms/inc/` | ✓ yes | Public `MmsValue` creation/manipulation API. |
| `mms_type_spec.h` | `src/mms/inc/` | ✓ yes | Public `MmsVariableSpecification_*` query helpers. |
| `mms_server_libinternal.h` | `src/mms/inc_private/` | **✗ no** | Contains `MmsServer_create`, `MmsServer_startListening`, `MmsServer_installReadHandler`, `MmsServer_installWriteHandler`, `MmsServer_insertIntoCache`, etc. |
| `mms_device_model.h` | `src/mms/inc_private/` | **✗ no** | Contains `MmsDevice_create`, `MmsDomain_create`, `MmsDomain_getName`, and the full `struct sMmsDomain` layout. |

### Conclusion on the public API

`IedServer` (the public, documented API) creates an MMS server internally but forces IEC 61850-style variable names: `<LogicalDevice>/<LN>$<FC>$<DO>$<DA>`. There is no public API that allows creating a domain with a flat variable name like `boolean`.

`MmsServer_create` exists but is only declared in the **private** header `mms_server_libinternal.h`. This header is not installed by `cmake install`.

### Symbol visibility

libiec61850's cmake build adds `-fvisibility=hidden` by default and marks
`LIB61850_INTERNAL` as `__attribute__((visibility("hidden")))`.  This prevents
calling internal functions even from a static library link.

To work around this, the Dockerfile rebuilds libiec61850 with:
```
-DCMAKE_C_FLAGS="-fvisibility=default" -DCMAKE_CXX_FLAGS="-fvisibility=default"
```
This makes all symbols visible, at the cost of preventing GCC's dead-stripping
optimisations for hidden symbols.  This is acceptable in a containerised build
where binary size is not critical.

### Decision: Fallback B — private headers + fvisibility=default

Because the Dockerfile builds libIEC61850 from source and retains the source tree during the adapter compilation stage, the private headers are accessible within the container. We add `src/mms/inc_private/` and `src/common/inc/` to the include path of the adapter CMake build.

This is accepted as a maintenance risk: upgrading libIEC61850 may require API adjustments. The risk is bounded because:
- The private headers change infrequently between minor releases.
- The adapter is tested in CI against a pinned commit SHA.
- All private-header usage is isolated to the libiec61850 adapter.

### Read/Write handler behaviour

- `MmsReadVariableHandler` signature: `MmsValue* (*)(void* param, MmsDomain* domain, char* variableId, MmsServerConnection conn, bool isDirectAccess)`. Returns a **newly allocated** `MmsValue*`; ownership is transferred to the server. Return `NULL` to signal "object undefined".
- `MmsWriteVariableHandler` signature: `MmsDataAccessError (*)(void* param, MmsDomain* domain, const char* variableId, int arrayIdx, const char* componentId, MmsValue* value, MmsServerConnection conn)`. The `value` is owned by the caller; clone if you need to keep it.
- `MmsServer_insertIntoCache` pre-populates the server value cache. Reads that are NOT intercepted by a read handler fall through to the cache.

### Threading model

Even with `CONFIG_MMS_THREADLESS_STACK=OFF` (the cmake default), the Release
cmake build of libIEC61850 v1.6.1 compiles only the **threadless** server
variants: `MmsServer_startListeningThreadless` / `MmsServer_stopListeningThreadless`.
The threaded variants (`MmsServer_startListening` / `MmsServer_stopListening`)
are not present in the static library.

Consequence: `mms_server.c` runs the MMS server in the **main thread** using
a `while (g_running)` polling loop that drives
`MmsServer_handleIncomingMessages` + `MmsServer_handleBackgroundTasks`.

### Chosen implementation strategy

1. Build `MmsDevice` → `MmsDomain`(s) → `MmsVariableSpecification`(s) from the fixture JSON using private-header APIs.
2. Pre-populate the value cache with `MmsServer_insertIntoCache`.
3. Install a write handler only (reads use the cache; write handler validates writability and updates the cache via `MmsServer_getValueFromCache` + `MmsValue_update`).
4. Set server identity with `MmsServer_setServerIdentity`.
5. Start the server with `MmsServer_startListeningThreadless` (main-thread polling).
6. Poll `MmsServer_isRunning` until true, then emit `{"event":"ready","address":"<addr>"}` on stdout.

### Supported types (Phase 1A)

All eight scalar types from the fixture are handled by the C server:

| Fixture type | MmsType | Value constructor |
|---|---|---|
| `boolean` | `MMS_BOOLEAN` | `MmsValue_newBoolean` |
| `integer` | `MMS_INTEGER` | `MmsValue_newIntegerFromInt32` |
| `unsigned` | `MMS_UNSIGNED` | `MmsValue_newUnsignedFromUint32` |
| `float32` | `MMS_FLOAT` | `MmsValue_newFloat` |
| `visible-string` | `MMS_VISIBLE_STRING` | `MmsValue_newVisibleString` |
| `octet-string` | `MMS_OCTET_STRING` | `MmsValue_newOctetString` + `MmsValue_setOctetString` |
| `bit-string` | `MMS_BIT_STRING` | `MmsValue_newBitString` + `MmsValue_setBitStringBit` |
| `utc-time` | `MMS_UTC_TIME` | `MmsValue_newUtcTimeByMsTime` |

Array and structure support deferred to Phase 1C.
