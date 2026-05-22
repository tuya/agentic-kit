#!/usr/bin/env bash
#
# tests/run_e2e.sh -- End-to-end test runner for ai-tcp-sdk examples.
#
# Builds the project, runs each example against the live Tuya AI server,
# and checks exit codes + expected output patterns.
#
# Usage:
#   ./tests/run_e2e.sh                  # run all e2e tests
#   ./tests/run_e2e.sh threaded_chat    # run one test
#   ./tests/run_e2e.sh --list           # list available tests
#   ./tests/run_e2e.sh --skip-build     # skip cmake build step
#
# Credentials:
#   The iot-sdk examples (threaded_chat, opus_chat, edu_camera) use
#   hardcoded test-device defaults.  Override via .env or env vars:
#     E2E_DEVID, E2E_SECRET_KEY, E2E_LOCAL_KEY
#
#   text_query needs TAI_CLIENT_ID + TAI_LOCAL_KEY (from MQTT proto-9000).
#   It is skipped unless those vars are set.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
E2E_LOG_DIR="$BUILD_DIR/e2e-logs"

# ---------------------------------------------------------------------------
# Colors (disabled if not a terminal)
# ---------------------------------------------------------------------------
if [ -t 1 ]; then
    GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[0;33m'
    CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'
else
    GREEN=''; RED=''; YELLOW=''; CYAN=''; BOLD=''; RESET=''
fi

# ---------------------------------------------------------------------------
# Load .env if present
# ---------------------------------------------------------------------------
if [ -f "$PROJECT_DIR/.env" ]; then
    set -a
    # shellcheck disable=SC1091
    . "$PROJECT_DIR/.env"
    set +a
fi

# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------
TIMEOUT_SEC=${E2E_TIMEOUT:-90}
SKIP_BUILD=0
PASSED=0
FAILED=0
SKIPPED=0

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
log()  { printf "${CYAN}[e2e]${RESET} %s\n" "$*"; }
pass() { printf "${GREEN}  PASS${RESET}  %s\n" "$1"; PASSED=$((PASSED + 1)); }
fail() { printf "${RED}  FAIL${RESET}  %s  (%s)\n" "$1" "$2"; FAILED=$((FAILED + 1)); }
skip() { printf "${YELLOW}  SKIP${RESET}  %s  (%s)\n" "$1" "$2"; SKIPPED=$((SKIPPED + 1)); }

dump_tail() {
    printf "    last 10 lines of output:\n"
    tail -10 "$1" | sed 's/^/    /'
}

# ---------------------------------------------------------------------------
# run_test NAME BINARY [--require PAT]... [--forbid PAT]... [--timeout S]
#          [-- BINARY_ARGS...]
#
#   Runs BINARY with BINARY_ARGS, captures output, checks:
#     1. Exit code == 0
#     2. Output contains "Connected" and "Done" (baseline)
#     3. All --require patterns found (extended regex)
#     4. No --forbid patterns found (extended regex)
# ---------------------------------------------------------------------------
run_test() {
    local name="$1"; shift
    local binary="$1"; shift

    local require_pats=()
    local forbid_pats=()
    local test_timeout="$TIMEOUT_SEC"

    while [ $# -gt 0 ]; do
        case "$1" in
            --require) require_pats+=("$2"); shift 2 ;;
            --forbid)  forbid_pats+=("$2");  shift 2 ;;
            --timeout) test_timeout="$2";    shift 2 ;;
            --)        shift; break ;;
            *)         break ;;
        esac
    done
    # remaining "$@" are binary args

    if [ ! -x "$binary" ]; then
        fail "$name" "binary not found: $binary"
        return
    fi

    log "Running: $name"
    local log_file="$E2E_LOG_DIR/${name}.log"
    local rc=0

    local timeout_cmd=""
    if command -v gtimeout >/dev/null 2>&1; then
        timeout_cmd="gtimeout"
    elif command -v timeout >/dev/null 2>&1; then
        timeout_cmd="timeout"
    fi

    if [ -n "$timeout_cmd" ]; then
        $timeout_cmd "${test_timeout}s" "$binary" "$@" > "$log_file" 2>&1 || rc=$?
    else
        "$binary" "$@" > "$log_file" 2>&1 || rc=$?
    fi

    if [ "$rc" -eq 124 ]; then
        fail "$name" "timed out after ${test_timeout}s"
        dump_tail "$log_file"
        return
    fi
    if [ "$rc" -ne 0 ]; then
        fail "$name" "exit code $rc"
        dump_tail "$log_file"
        return
    fi

    # Pattern checks
    local problems=""
    add_problem() {
        [ -n "$problems" ] && problems="$problems, "
        problems="${problems}$1"
    }

    # Baseline: "Connected" and "Done" must appear.
    grep -q "Connected" "$log_file" || add_problem "missing 'Connected'"
    grep -q "Done"      "$log_file" || add_problem "missing 'Done'"

    for pat in "${require_pats[@]+"${require_pats[@]}"}"; do
        grep -qE -- "$pat" "$log_file" || add_problem "missing '$pat'"
    done

    for pat in "${forbid_pats[@]+"${forbid_pats[@]}"}"; do
        grep -qE -- "$pat" "$log_file" && add_problem "forbidden '$pat'"
    done

    if [ -n "$problems" ]; then
        fail "$name" "$problems"
        dump_tail "$log_file"
        return
    fi

    pass "$name"
}

# ---------------------------------------------------------------------------
# check_output_file NAME PATH MIN_BYTES [MAX_BYTES]
# ---------------------------------------------------------------------------
check_output_file() {
    local name="$1" path="$2" min_bytes="$3" max_bytes="${4:-}"
    if [ ! -f "$path" ]; then
        fail "${name}:output" "missing file: $path"
        return 1
    fi
    local size
    size=$(wc -c < "$path" | tr -d ' ')
    if [ "$size" -lt "$min_bytes" ]; then
        fail "${name}:output" "$path too small: ${size} < ${min_bytes}"
        return 1
    fi
    if [ -n "$max_bytes" ] && [ "$size" -gt "$max_bytes" ]; then
        fail "${name}:output" "$path too large: ${size} > ${max_bytes}"
        return 1
    fi
    log "  ${name}: $path = ${size} bytes"
    return 0
}

# Common forbidden patterns (protocol errors that should never appear).
FORBID_PROTO=(
    --forbid "HMAC verify failed"
    --forbid "packet decode failed"
    --forbid "fragment overflow"
)

# ---------------------------------------------------------------------------
# Test definitions
# ---------------------------------------------------------------------------

test_threaded_chat() {
    local bin="$BUILD_DIR/tai_threaded_chat"
    local args=()
    [ -n "${E2E_DEVID:-}" ]      && args+=("$E2E_DEVID")
    [ -n "${E2E_SECRET_KEY:-}" ] && args+=("$E2E_SECRET_KEY")
    [ -n "${E2E_LOCAL_KEY:-}" ]  && args+=("$E2E_LOCAL_KEY")

    # Pattern notes:
    #   The structured packet logger (src/tai_pkt_log.c) emits one JSON
    #   line per non-media packet at INFO.  Patterns below match the JSON
    #   wire fields instead of free-text log messages.
    run_test "threaded_chat" "$bin" \
        --require "Session token acquired" \
        --require "\[pkt\] send:.*\"packet-type\":\"client-hello\"" \
        --require "\[pkt\] send:.*\"packet-type\":\"session-new\"" \
        --require "\[pkt\] receive:.*\"packet-type\":\"authenticate-response\"" \
        --require "worker thread started" \
        --require "connected successfully" \
        --require "send_text: [0-9]+ bytes" \
        --require "tai_send_text returned 0" \
        --require "\[pkt\] send:.*\"event-type\":\"start\"" \
        --require "\[pkt\] send:.*\"event-type\":\"payloads-end\"" \
        --require "\[pkt\] send:.*\"event-type\":\"end\"" \
        --require "\[pkt\] receive:.*\"packet-type\":\"event\".*\"event-type\":\"start\"" \
        --require "\[pkt\] receive:.*\"packet-type\":\"event\".*\"event-type\":\"end\"" \
        --require "\[pkt\] receive:.*\"packet-type\":\"text\".*\"stream-flag\":\"start\"" \
        --require "\[pkt\] receive:.*\"packet-type\":\"text\".*\"stream-flag\":\"end\"" \
        --require "worker thread joined" \
        "${FORBID_PROTO[@]}" \
        -- "${args[@]+"${args[@]}"}"
}

test_opus_chat() {
    local bin="$BUILD_DIR/tai_opus_chat"
    if [ ! -x "$bin" ]; then
        skip "opus_chat" "binary not built (needs libopus)"
        return
    fi

    local args=("-")
    [ -n "${E2E_DEVID:-}" ]      && args+=("$E2E_DEVID")
    [ -n "${E2E_SECRET_KEY:-}" ] && args+=("$E2E_SECRET_KEY")
    [ -n "${E2E_LOCAL_KEY:-}" ]  && args+=("$E2E_LOCAL_KEY")

    local output_pcm="$PROJECT_DIR/output_opus_chat.pcm"
    rm -f "$output_pcm"

    run_test "opus_chat" "$bin" \
        --require "Session token acquired" \
        --require "\[pkt\] send:.*\"packet-type\":\"client-hello\"" \
        --require "\[pkt\] send:.*\"packet-type\":\"session-new\"" \
        --require "\[pkt\] receive:.*\"packet-type\":\"authenticate-response\"" \
        --require "worker thread started" \
        --require "connected successfully" \
        --require "send_text: [0-9]+ bytes" \
        --require "Send complete" \
        --require "Waiting for AI response" \
        --require "\[pkt\] send:.*\"event-type\":\"start\"" \
        --require "\[pkt\] send:.*\"event-type\":\"end\"" \
        --require "\[pkt\] receive:.*\"packet-type\":\"event\".*\"event-type\":\"start\"" \
        --require "\[pkt\] receive:.*\"packet-type\":\"event\".*\"event-type\":\"end\"" \
        --require "\[pkt\] receive:.*\"packet-type\":\"text\".*\"stream-flag\":\"start\"" \
        --require "\[pkt\] receive:.*\"packet-type\":\"text\".*\"stream-flag\":\"end\"" \
        --require "receive-audio:.*\"packet-type\":\"audio\".*\"order\":0.*\"stream-flag\":\"start\"" \
        --require "receive-audio:.*\"packet-type\":\"audio\".*\"stream-flag\":\"end\"" \
        --require "\[TTS audio stream started\]" \
        --require "Latency Stats" \
        --require "Send start  -> first text:" \
        --require "Send end    -> first audio:" \
        --require "First audio -> audio done:" \
        --require "TTS audio saved to:" \
        --require "worker thread joined" \
        "${FORBID_PROTO[@]}" \
        -- "${args[@]}"

    check_output_file "opus_chat" "$output_pcm" 1024 10485760 || true
}

test_edu_camera() {
    local bin="$BUILD_DIR/tai_edu_camera"

    local img="$PROJECT_DIR/examples/posix/res/test.jpg"
    if [ ! -f "$img" ]; then
        skip "edu_camera" "no input image: $img"
        return
    fi

    local output_pcm="$PROJECT_DIR/output_tts.pcm"
    local args=("$img" "image_recognition" "$output_pcm")
    [ -n "${E2E_DEVID_CAMERA:-}" ]      && args+=("$E2E_DEVID_CAMERA")
    [ -n "${E2E_SECRET_KEY_CAMERA:-}" ] && args+=("$E2E_SECRET_KEY_CAMERA")
    [ -n "${E2E_LOCAL_KEY_CAMERA:-}" ]  && args+=("$E2E_LOCAL_KEY_CAMERA")

    rm -f "$output_pcm"

    run_test "edu_camera" "$bin" \
        --require "\[pkt\] send:.*\"packet-type\":\"session-new\"" \
        --require "send_image" \
        --require "\[pkt\] send:.*\"packet-type\":\"image\"" \
        --require "receive-audio:.*\"packet-type\":\"audio\".*\"order\":0.*\"stream-flag\":\"start\"" \
        --require "receive-audio:.*\"packet-type\":\"audio\".*\"stream-flag\":\"end\"" \
        "${FORBID_PROTO[@]}" \
        -- "${args[@]}"

    check_output_file "edu_camera" "$output_pcm" 1024 10485760 || true
}

test_text_query() {
    local bin="$BUILD_DIR/tai_text_query"

    if [ -z "${TAI_CLIENT_ID:-}" ] || [ -z "${TAI_LOCAL_KEY:-}" ]; then
        skip "text_query" "TAI_CLIENT_ID and TAI_LOCAL_KEY not set"
        return
    fi

    export TAI_QUERY="Hello, this is an automated test."

    run_test "text_query" "$bin" \
        --require "Response:|\\[disconnected|EVT_END" \
        --forbid "HMAC verify failed" \
        --forbid "packet decode failed"
}

ALL_TESTS=(threaded_chat opus_chat edu_camera text_query)

# ---------------------------------------------------------------------------
# CLI parsing
# ---------------------------------------------------------------------------
TESTS_TO_RUN=()

for arg in "$@"; do
    case "$arg" in
        --skip-build) SKIP_BUILD=1 ;;
        --list)
            printf "Available E2E tests:\n"
            for t in "${ALL_TESTS[@]}"; do printf "  %s\n" "$t"; done
            exit 0
            ;;
        --help|-h)
            printf "Usage: %s [--skip-build] [--list] [test_name ...]\n" "$0"
            printf "\nRuns E2E tests for ai-tcp-sdk examples.\n"
            printf "If no test names given, runs all tests.\n"
            printf "\nCredentials: set in .env or env vars (see .env.example).\n"
            exit 0
            ;;
        *)
            TESTS_TO_RUN+=("$arg")
            ;;
    esac
done

if [ ${#TESTS_TO_RUN[@]} -eq 0 ]; then
    TESTS_TO_RUN=("${ALL_TESTS[@]}")
fi

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
if [ "$SKIP_BUILD" -eq 0 ]; then
    log "Building project..."
    cmake -B "$BUILD_DIR" -DTAI_PAL_OPENSSL=ON -DTAI_BUILD_EXAMPLES=ON \
          -DTAI_IOT_CHAT=ON -DTAI_OPUS_CHAT=ON \
          -S "$PROJECT_DIR" > /dev/null 2>&1
    cmake --build "$BUILD_DIR" -j "$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" 2>&1 | tail -3
    log "Build OK"
fi

mkdir -p "$E2E_LOG_DIR"

# ---------------------------------------------------------------------------
# Run tests
# ---------------------------------------------------------------------------
printf "\n${BOLD}=== ai-tcp-sdk E2E Tests ===${RESET}\n\n"

for name in "${TESTS_TO_RUN[@]}"; do
    func="test_${name}"
    if declare -f "$func" >/dev/null 2>&1; then
        "$func"
    else
        printf "${RED}  ERROR${RESET}  Unknown test: %s\n" "$name"
        FAILED=$((FAILED + 1))
    fi
done

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
printf "\n${BOLD}=== Summary ===${RESET}\n"
printf "  ${GREEN}Passed:${RESET}  %d\n" "$PASSED"
printf "  ${RED}Failed:${RESET}  %d\n" "$FAILED"
printf "  ${YELLOW}Skipped:${RESET} %d\n" "$SKIPPED"
printf "  Logs:    %s/\n" "$E2E_LOG_DIR"

if [ "$FAILED" -gt 0 ]; then
    printf "\n${RED}Some tests failed.${RESET} Check logs above.\n"
    exit 1
fi

printf "\n${GREEN}All tests passed.${RESET}\n"
exit 0
