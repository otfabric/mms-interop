#!/usr/bin/env bash
# smoke-test.sh — adapter self-tests for mms-interop.
#
# Verifies that each adapter command:
#   1. Starts without error.
#   2. Emits a valid JSON ready event (for server commands).
#   3. Emits valid JSON Lines output (for client commands).
#   4. Exits with the expected code.
#   5. Ready event contains the expected fields: event, address, fixture, adapter, version.
#
# Usage:
#   LIBIEC61850_IMAGE=mms-interop-libiec61850:local \
#   IEC61850BEAN_IMAGE=mms-interop-iec61850bean:local \
#   ./scripts/smoke-test.sh
#
# Exit code: 0 if all checks pass, 1 if any check fails.

set -euo pipefail

LIBIEC61850_IMAGE="${LIBIEC61850_IMAGE:-mms-interop-libiec61850:local}"
IEC61850BEAN_IMAGE="${IEC61850BEAN_IMAGE:-mms-interop-iec61850bean:local}"

PASS=0
FAIL=0

# Colours (suppressed when not a tty)
if [ -t 1 ]; then
    RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[0;33m'; RESET='\033[0m'
else
    RED=''; GREEN=''; YELLOW=''; RESET=''
fi

pass() { echo -e "${GREEN}PASS${RESET} $*"; PASS=$((PASS + 1)); }
fail() { echo -e "${RED}FAIL${RESET} $*"; FAIL=$((FAIL + 1)); }
info() { echo -e "${YELLOW}INFO${RESET} $*"; }

# ---------------------------------------------------------------------------
# Helper: start a server container, capture the ready line, return it.
# Usage: ready_line=$(start_server <image> <command> <port> [extra args...])
# The container is stopped and removed automatically on function return.
# ---------------------------------------------------------------------------
start_server() {
    local image="$1" cmd="$2" host_port="$3"
    shift 3
    local extra=("$@")

    local cid
    cid=$(docker run -d \
        -p "${host_port}:1102" \
        --entrypoint "${cmd}" \
        "${image}" \
        --port 1102 \
        "${extra[@]+"${extra[@]}"}" \
    )

    # Wait up to 30 s for the ready event.
    local ready=""
    local deadline=$((SECONDS + 30))
    while [ $SECONDS -lt $deadline ]; do
        ready=$(docker logs "$cid" 2>/dev/null | grep '"event":"ready"' | head -1 || true)
        [ -n "$ready" ] && break
        sleep 0.2
    done

    docker stop "$cid" >/dev/null 2>&1
    docker rm   "$cid" >/dev/null 2>&1

    printf '%s' "$ready"
}

# ---------------------------------------------------------------------------
# Helper: run a client container against a server and capture stdout.
# Usage: output=$(run_client <net> <image> <command> <host> [extra args...])
# ---------------------------------------------------------------------------
run_client_pair() {
    local image_server="$1" cmd_server="$2"
    local image_client="$3" cmd_client="$4"
    local net="smoke-$$"

    docker network create "$net" >/dev/null 2>&1

    # Start server.
    local sid
    sid=$(docker run -d \
        --network "$net" \
        --name "smoke-server-$$" \
        --entrypoint "${cmd_server}" \
        "${image_server}" \
        --port 1102)

    # Wait for server ready.
    local deadline=$((SECONDS + 30))
    while [ $SECONDS -lt $deadline ]; do
        docker logs "$sid" 2>/dev/null | grep -q '"event":"ready"' && break
        sleep 0.2
    done

    # Run client.
    local output
    output=$(docker run --rm \
        --network "$net" \
        --entrypoint "${cmd_client}" \
        "${image_client}" \
        --host "smoke-server-$$" --port 1102 \
        2>/dev/null || true)

    docker stop "$sid" >/dev/null 2>&1
    docker rm   "$sid" >/dev/null 2>&1
    docker network rm "$net" >/dev/null 2>&1

    printf '%s' "$output"
}

# ---------------------------------------------------------------------------
# Check: ready event JSON has required fields with non-empty values.
# ---------------------------------------------------------------------------
check_ready_event() {
    local label="$1" ready="$2"

    if [ -z "$ready" ]; then
        fail "${label}: no ready event received within 30 s"
        return
    fi

    # Validate JSON syntax.
    if ! echo "$ready" | python3 -c "import sys,json; json.load(sys.stdin)" 2>/dev/null; then
        fail "${label}: ready event is not valid JSON: ${ready}"
        return
    fi

    # Check required fields.
    local missing=()
    for field in event address fixture adapter version; do
        value=$(echo "$ready" | python3 -c \
            "import sys,json; d=json.load(sys.stdin); print(d.get('${field}',''))" 2>/dev/null)
        [ -z "$value" ] && missing+=("$field")
    done

    if [ ${#missing[@]} -gt 0 ]; then
        fail "${label}: ready event missing fields: ${missing[*]}"
        fail "  got: ${ready}"
    else
        pass "${label}: ready event OK — $(echo "$ready" | python3 -c \
            "import sys,json; d=json.load(sys.stdin); \
             print('adapter={}, fixture={}, version={}'.format(d['adapter'],d['fixture'],d['version']))")"
    fi
}

# ---------------------------------------------------------------------------
# Check: client output is non-empty valid JSON Lines.
# ---------------------------------------------------------------------------
check_json_lines() {
    local label="$1" output="$2"

    if [ -z "$output" ]; then
        fail "${label}: no JSON Lines output"
        return
    fi

    local bad=0
    while IFS= read -r line; do
        [ -z "$line" ] && continue
        if ! echo "$line" | python3 -c "import sys,json; json.load(sys.stdin)" 2>/dev/null; then
            bad=$((bad + 1))
            fail "${label}: invalid JSON line: ${line}"
        fi
    done <<< "$output"

    [ $bad -eq 0 ] && pass "${label}: JSON Lines OK ($(echo "$output" | grep -c .) lines)"
}

# ---------------------------------------------------------------------------
# Check: required binary exists inside image.
# ---------------------------------------------------------------------------
check_binary() {
    local image="$1" bin="$2"
    if docker run --rm --entrypoint sh "${image}" -c "command -v ${bin}" >/dev/null 2>&1; then
        pass "binary ${bin} in ${image}"
    else
        fail "binary ${bin} MISSING in ${image}"
    fi
}

# ---------------------------------------------------------------------------
# Check: fixture file exists inside image.
# ---------------------------------------------------------------------------
check_fixture_file() {
    local image="$1" path="$2"
    if docker run --rm --entrypoint sh "${image}" -c "test -f ${path}" >/dev/null 2>&1; then
        pass "fixture ${path} in ${image}"
    else
        fail "fixture ${path} MISSING in ${image}"
    fi
}

# ===========================================================================
# Tests
# ===========================================================================

info "=== Binary presence ==="
check_binary "$LIBIEC61850_IMAGE" libiec61850-mms-server
check_binary "$LIBIEC61850_IMAGE" libiec61850-mms-client
check_binary "$LIBIEC61850_IMAGE" libiec61850-ied-server
check_binary "$LIBIEC61850_IMAGE" libiec61850-ied-client
check_binary "$LIBIEC61850_IMAGE" libiec61850-ied-reporter
check_binary "$IEC61850BEAN_IMAGE" iec61850bean-ied-server
check_binary "$IEC61850BEAN_IMAGE" iec61850bean-ied-client

info ""
info "=== Fixture files ==="
check_fixture_file "$LIBIEC61850_IMAGE" /fixtures/mms/interop.json
check_fixture_file "$LIBIEC61850_IMAGE" /fixtures/iec61850/interop.icd
check_fixture_file "$LIBIEC61850_IMAGE" /fixtures/iec61850/values.json
check_fixture_file "$IEC61850BEAN_IMAGE" /fixtures/iec61850/interop.icd
check_fixture_file "$IEC61850BEAN_IMAGE" /fixtures/iec61850/values.json

info ""
info "=== Server ready events ==="

ready=$(start_server "$LIBIEC61850_IMAGE" libiec61850-mms-server 19001)
check_ready_event "libiec61850-mms-server" "$ready"

ready=$(start_server "$LIBIEC61850_IMAGE" libiec61850-ied-server 19002 \
    --icd /fixtures/iec61850/interop.icd --values /fixtures/iec61850/values.json)
check_ready_event "libiec61850-ied-server" "$ready"

ready=$(start_server "$IEC61850BEAN_IMAGE" iec61850bean-ied-server 19003)
check_ready_event "iec61850bean-ied-server" "$ready"

info ""
info "=== Client JSON Lines output ==="

output=$(run_client_pair \
    "$LIBIEC61850_IMAGE" libiec61850-mms-server \
    "$LIBIEC61850_IMAGE" libiec61850-mms-client)
check_json_lines "libiec61850-mms-client" "$output"

output=$(run_client_pair \
    "$LIBIEC61850_IMAGE" libiec61850-ied-server \
    "$LIBIEC61850_IMAGE" libiec61850-ied-client)
check_json_lines "libiec61850-ied-client" "$output"

output=$(run_client_pair \
    "$LIBIEC61850_IMAGE" libiec61850-ied-server \
    "$LIBIEC61850_IMAGE" libiec61850-ied-reporter)
check_json_lines "libiec61850-ied-reporter" "$output"

output=$(run_client_pair \
    "$IEC61850BEAN_IMAGE" iec61850bean-ied-server \
    "$IEC61850BEAN_IMAGE" iec61850bean-ied-client)
check_json_lines "iec61850bean-ied-client" "$output"

# ===========================================================================
# Summary
# ===========================================================================

echo ""
echo "Results: ${GREEN}${PASS} passed${RESET}  ${RED}${FAIL} failed${RESET}"

[ $FAIL -eq 0 ]
