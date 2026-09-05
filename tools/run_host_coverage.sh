#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
project_dir="$(dirname "$script_dir")"
build_dir="${ESP_MINER_HOST_COVERAGE_BUILD_DIR:-$project_dir/build/host-coverage}"
report_dir="${ESP_MINER_HOST_COVERAGE_REPORT_DIR:-$build_dir/coverage}"
minimum_line_coverage="${ESP_MINER_COVERAGE_MIN_LINE:-58}"
minimum_branch_coverage="${ESP_MINER_COVERAGE_MIN_BRANCH:-49}"
minimum_breadth_coverage="${ESP_MINER_COVERAGE_MIN_BREADTH:-11.8}"

if ! command -v gcovr >/dev/null 2>&1; then
    printf '%s\n' \
        'gcovr 8.6 is required for native coverage.' \
        'Install it with: python3 -m pip install -r host-tests/requirements.txt' >&2
    exit 1
fi

export CC="${CC:-gcc}"

python3 "$project_dir/tools/test_inventory.py" --check
python3 -m unittest discover \
    -s "$project_dir/tools/tests" \
    -p 'test_*.py'

cmake \
    -S "$project_dir/host-tests" \
    -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DESP_MINER_ENABLE_SANITIZERS=OFF \
    -DESP_MINER_ENABLE_COVERAGE=ON

# Coverage counters must describe this invocation, not an earlier local run.
# Only this target is instrumented, so keep deletion narrowly scoped to its
# generated object directory.
coverage_object_dir="$build_dir/CMakeFiles/esp_miner_host_tests.dir"
if [[ -d "$coverage_object_dir" ]]; then
    find "$coverage_object_dir" -type f -name '*.gcda' -delete
fi

cmake --build "$build_dir" --target esp_miner_host_tests --parallel
ctest --test-dir "$build_dir" --output-on-failure

mkdir -p "$report_dir"

# Inventory every repository-owned production source in the two firmware
# source roots. Do not enumerate first-party modules: new files must enter the
# denominator automatically. Only copied third-party sources are excluded.
gcovr_status=0
gcovr \
    --root "$project_dir" \
    --object-directory "$build_dir" \
    --filter "$project_dir/components/" \
    --filter "$project_dir/main/" \
    --include '.*\.(c|cc|cpp|cxx)$' \
    --exclude '.*/test/.*' \
    --exclude "$project_dir/components/libsecp256k1/.*" \
    --exclude "$project_dir/components/dns_server/dns_server.c" \
    --exclude "$project_dir/components/stratum/base58.c" \
    --exclude "$project_dir/components/stratum/segwit_addr.c" \
    --exclude '.*/node_modules/.*' \
    --exclude-directory '.*/libsecp256k1($|/)' \
    --exclude-directory '.*/node_modules($|/)' \
    --exclude-directory '.*/test($|/)' \
    --exclude-directory "$build_dir/_deps($|/)" \
    --txt "$report_dir/coverage.txt" \
    --html-details "$report_dir/index.html" \
    --html-title 'ESP-Miner native host coverage' \
    --json "$report_dir/coverage.json" \
    --json-pretty \
    --json-summary "$report_dir/gcovr-summary.json" \
    --json-summary-pretty \
    --print-summary \
    --fail-under-line "$minimum_line_coverage" \
    --fail-under-branch "$minimum_branch_coverage" \
    "$build_dir" \
    "$project_dir/components" \
    "$project_dir/main" || gcovr_status=$?

breadth_status=0
python3 "$project_dir/tools/coverage_summary.py" \
    "$report_dir/gcovr-summary.json" \
    --text-output "$report_dir/coverage-summary.txt" \
    --json-output "$report_dir/coverage-summary.json" \
    --markdown-output "$report_dir/coverage-summary.md" \
    --fail-under-breadth "$minimum_breadth_coverage" \
    --required-file-branch-floor 90 \
    --require-fully-covered-file components/stratum/sv1_protocol.c || breadth_status=$?

printf 'Coverage reports: %s\n' "$report_dir"

if ((gcovr_status != 0)); then
    exit "$gcovr_status"
fi
exit "$breadth_status"
