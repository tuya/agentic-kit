#!/bin/bash
# Linux Memory Leak Check using Valgrind
# Usage: ./run_valgrind_check.sh <test_executable> [args...]
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
    exit 2
fi

TEST_EXEC="$1"
shift

echo "=========================================="
echo "Memory Leak Check (Valgrind)"
echo "Executable: $TEST_EXEC"
echo "Arguments: $@"
echo "=========================================="
echo ""

# Check if valgrind is installed
if ! command -v valgrind &> /dev/null; then
    echo "ERROR: valgrind is not installed"
    echo "Install with: sudo apt-get install valgrind"
    exit 2
fi

# Create temp file for valgrind output
VALGRIND_OUTPUT=$(mktemp)

# Run valgrind
valgrind \
    --leak-check=full \
    --show-leak-kinds=all \
    --errors-for-leak-kinds=definite,indirect \
    --error-exitcode=99 \
    --log-file="$VALGRIND_OUTPUT" \
    "$TEST_EXEC" "$@"

TEST_EXIT_CODE=$?

# Display valgrind output
cat "$VALGRIND_OUTPUT"
echo ""

# Parse valgrind output
DEFINITELY_LOST=$(grep "definitely lost:" "$VALGRIND_OUTPUT" | grep -oE "[0-9,]+ bytes" | head -1)
INDIRECTLY_LOST=$(grep "indirectly lost:" "$VALGRIND_OUTPUT" | grep -oE "[0-9,]+ bytes" | head -1)
POSSIBLY_LOST=$(grep "possibly lost:" "$VALGRIND_OUTPUT" | grep -oE "[0-9,]+ bytes" | head -1)
STILL_REACHABLE=$(grep "still reachable:" "$VALGRIND_OUTPUT" | grep -oE "[0-9,]+ bytes" | head -1)

# Check for "All heap blocks were freed"
if grep -q "All heap blocks were freed -- no leaks are possible" "$VALGRIND_OUTPUT"; then
    echo "=========================================="
    echo "RESULT: NO MEMORY LEAKS DETECTED"
    echo "=========================================="
    rm -f "$VALGRIND_OUTPUT"
    exit 0
fi

# Check for definite/indirect leaks
if grep -q "definitely lost: [1-9]" "$VALGRIND_OUTPUT" || grep -q "indirectly lost: [1-9]" "$VALGRIND_OUTPUT"; then
    echo "=========================================="
    echo "RESULT: MEMORY LEAKS DETECTED"
    echo "  Definitely lost: $DEFINITELY_LOST"
    echo "  Indirectly lost: $INDIRECTLY_LOST"
    echo "  Possibly lost:   $POSSIBLY_LOST"
    echo "  Still reachable: $STILL_REACHABLE"
    echo "=========================================="
    rm -f "$VALGRIND_OUTPUT"
    exit 1
fi

# No definite leaks
if grep -q "definitely lost: 0 bytes" "$VALGRIND_OUTPUT"; then
    echo "=========================================="
    echo "RESULT: NO MEMORY LEAKS DETECTED"
    echo "  (possibly lost/still reachable are not considered leaks)"
    echo "=========================================="
    rm -f "$VALGRIND_OUTPUT"
    exit 0
fi

# Unknown result
echo "=========================================="
echo "RESULT: UNKNOWN - Could not parse output"
echo "Test exit code: $TEST_EXIT_CODE"
echo "=========================================="
rm -f "$VALGRIND_OUTPUT"
exit 2
