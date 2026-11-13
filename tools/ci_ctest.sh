#!/bin/sh
# Shared ctest runner for the CI jobs (test, test:sanitizers).
#
# Runs ctest serially with output-on-failure, then applies the empty-list
# count guard. The guard exists because the CI image's ctest does not honour
# --test-dir and may predate --no-tests=error: an empty test list would
# otherwise pass silently (the enable_testing() note in CMakeLists.txt
# explains how the CTestTestfile list can go stale). Serial only on purpose
# -- the mock-driven suites bind compile-time ports (AGENTS.md: "ctest -j is
# unsupported").
#
# Usage: ci_ctest.sh <build-dir> <timeout-sec> <context>
#   <context> names the tree in the zero-tests error message.
set -u

DIR=${1:?usage: ci_ctest.sh <build-dir> <timeout-sec> <context>}
TIMEOUT=${2:?missing timeout-sec}
CONTEXT=${3:?missing context}

cd "${DIR}" || exit 1
ctest --output-on-failure --timeout "${TIMEOUT}"
rc=$?
TOTAL=$(ctest -N | awk '/Total Tests:/ {print $NF}')
if [ "${TOTAL:-0}" -eq 0 ]; then
    echo "ctest reports zero tests: ${CONTEXT}"
    exit 1
fi
exit $rc
