#!/bin/bash
# Run host-based unit tests
# Usage: ./scripts/run-host-tests.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
HOST_TEST_DIR="$PROJECT_ROOT/test/host"
BUILD_DIR="$HOST_TEST_DIR/build"

echo "========================================"
echo "  ACS ESP-Miner Host Test Runner"
echo "========================================"
echo ""

# Create build directory if needed
mkdir -p "$BUILD_DIR"

# Configure and build
echo "Configuring..."
cmake -B "$BUILD_DIR" -S "$HOST_TEST_DIR" -DCMAKE_BUILD_TYPE=Debug

echo ""
echo "Building..."
cmake --build "$BUILD_DIR" --parallel

echo ""
echo "Running tests..."
"$BUILD_DIR/run_tests"

exit_code=$?

echo ""
if [ $exit_code -eq 0 ]; then
    echo "All tests passed!"
else
    echo "Some tests failed!"
fi

exit $exit_code
