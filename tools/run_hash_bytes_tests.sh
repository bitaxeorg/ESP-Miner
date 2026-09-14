#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_BUILD_DIR"' EXIT

"${CC:-cc}" -std=c11 -O2 -g -Wall -Wextra -Werror -Wcast-align=strict \
    -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
    -I"$ROOT_DIR/components/stratum/include" \
    "$ROOT_DIR/components/stratum/hash_bytes.c" \
    "$ROOT_DIR/tools/tests/test_hash_bytes.c" \
    -lm -o "$TEST_BUILD_DIR/test_hash_bytes"
"$TEST_BUILD_DIR/test_hash_bytes"
