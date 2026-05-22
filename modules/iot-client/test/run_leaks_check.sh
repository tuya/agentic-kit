#!/bin/bash
# macOS Memory Leak Check using leaks tool
# Usage: ./run_leaks_check.sh <test_executable> [args...]
#
# Exit codes:
#   0 - No memory leaks detected
#   1 - Memory leaks detected
#   2 - Error running the tool

if [ -z "$1" ]; then
    echo "Usage: $0 <test_executable> [args...]"
    echo ""
    echo "Examples:"
    echo "  $0 ./mqtt_test"
    echo "  $0 ./dns_test"
    echo ""
    echo "Note: May require 'sudo' on modern macOS"
    echo "Note: Do NOT build with -DENABLE_ASAN=ON"
    exit 2
fi

TEST_EXEC="$1"
shift

echo "=========================================="
echo "Memory Leak Check (macOS leaks)"
echo "Executable: $TEST_EXEC"
echo "Arguments: $@"
echo "=========================================="
echo ""

# Run leaks and capture output
OUTPUT=$(leaks --atExit -- "$TEST_EXEC" "$@" 2>&1)
LEAKS_EXIT_CODE=$?

echo "$OUTPUT"
echo ""

# Parse output to check for leaks
if echo "$OUTPUT" | grep -q "0 leaks for 0 total leaked bytes"; then
    echo "=========================================="
    echo "RESULT: NO MEMORY LEAKS DETECTED"
    echo "=========================================="
    exit 0
elif echo "$OUTPUT" | grep -q "leaks for.*total leaked bytes"; then
    # Extract leak count
    LEAK_INFO=$(echo "$OUTPUT" | grep "leaks for.*total leaked bytes")
    echo "=========================================="
    echo "RESULT: MEMORY LEAKS DETECTED"
    echo "$LEAK_INFO"
    echo "=========================================="
    exit 1
elif echo "$OUTPUT" | grep -q "unable to inspect"; then
    echo "=========================================="
    echo "RESULT: ERROR - Cannot inspect process"
    echo "Try: sudo $0 $TEST_EXEC $@"
    echo "Or build without ASAN: cmake .. -DENABLE_ASAN=OFF"
    echo "=========================================="
    exit 2
else
    echo "=========================================="
    echo "RESULT: UNKNOWN - Could not parse output"
    echo "Exit code from leaks: $LEAKS_EXIT_CODE"
    echo "=========================================="
    exit 2
fi
