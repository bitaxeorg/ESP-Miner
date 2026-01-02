#!/bin/bash
# Pre-commit checks for ACS ESP-Miner
# Can be run standalone or via pre-commit framework
#
# Usage: ./scripts/pre-commit-checks.sh [--all | --format | --lint | --test]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Track overall status
FAILED=0

print_header() {
    echo ""
    echo -e "${YELLOW}========================================${NC}"
    echo -e "${YELLOW}  $1${NC}"
    echo -e "${YELLOW}========================================${NC}"
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

print_error() {
    echo -e "${RED}✗ $1${NC}"
}

# Check if a command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Find C/H files (excluding test infrastructure and build dirs)
find_source_files() {
    find "$PROJECT_ROOT" \( -name "*.c" -o -name "*.h" \) \
        -not -path "*/build/*" \
        -not -path "*/test/host/*" \
        -not -path "*/managed_components/*" \
        -type f
}

# Format check with clang-format
check_format() {
    print_header "Checking code formatting (clang-format)"

    if ! command_exists clang-format; then
        echo -e "${YELLOW}Warning: clang-format not installed, skipping format check${NC}"
        return 0
    fi

    local format_errors=0
    while IFS= read -r file; do
        if ! clang-format --dry-run --Werror "$file" 2>/dev/null; then
            print_error "Format issue: $file"
            format_errors=$((format_errors + 1))
        fi
    done < <(find_source_files)

    if [ $format_errors -eq 0 ]; then
        print_success "All files properly formatted"
        return 0
    else
        print_error "$format_errors file(s) have formatting issues"
        echo "Run: find . -name '*.c' -o -name '*.h' | xargs clang-format -i"
        return 1
    fi
}

# Static analysis with cppcheck
check_lint() {
    print_header "Running static analysis (cppcheck)"

    if ! command_exists cppcheck; then
        echo -e "${YELLOW}Warning: cppcheck not installed, skipping lint check${NC}"
        return 0
    fi

    if cppcheck \
        --error-exitcode=1 \
        --enable=warning,style,performance \
        --suppress=missingIncludeSystem \
        --suppress=unusedFunction \
        --inline-suppr \
        -I "$PROJECT_ROOT/main/" \
        -I "$PROJECT_ROOT/components/asic/include/" \
        -I "$PROJECT_ROOT/components/stratum/include/" \
        --quiet \
        "$PROJECT_ROOT/main/" \
        "$PROJECT_ROOT/components/" 2>&1; then
        print_success "Static analysis passed"
        return 0
    else
        print_error "Static analysis found issues"
        return 1
    fi
}

# Run host-based unit tests
check_tests() {
    print_header "Running host-based unit tests"

    if ! command_exists cmake; then
        echo -e "${YELLOW}Warning: cmake not installed, skipping host tests${NC}"
        return 0
    fi

    local test_dir="$PROJECT_ROOT/test/host"
    local build_dir="$test_dir/build"

    # Build tests
    echo "Building tests..."
    if ! cmake -B "$build_dir" -S "$test_dir" -DCMAKE_BUILD_TYPE=Debug >/dev/null 2>&1; then
        print_error "Failed to configure tests"
        return 1
    fi

    if ! cmake --build "$build_dir" --parallel >/dev/null 2>&1; then
        print_error "Failed to build tests"
        return 1
    fi

    # Run tests
    echo "Running tests..."
    if "$build_dir/run_tests"; then
        print_success "All tests passed"
        return 0
    else
        print_error "Some tests failed"
        return 1
    fi
}

# Check for trailing whitespace
check_whitespace() {
    print_header "Checking for trailing whitespace"

    local files_with_ws
    files_with_ws=$(find_source_files | xargs grep -l "[[:blank:]]$" 2>/dev/null || true)

    if [ -z "$files_with_ws" ]; then
        print_success "No trailing whitespace found"
        return 0
    else
        print_error "Files with trailing whitespace:"
        echo "$files_with_ws"
        return 1
    fi
}

# Main
main() {
    local run_all=true
    local run_format=false
    local run_lint=false
    local run_test=false

    # Parse arguments
    for arg in "$@"; do
        case $arg in
            --all)
                run_all=true
                ;;
            --format)
                run_all=false
                run_format=true
                ;;
            --lint)
                run_all=false
                run_lint=true
                ;;
            --test)
                run_all=false
                run_test=true
                ;;
            --help)
                echo "Usage: $0 [--all | --format | --lint | --test]"
                echo ""
                echo "Options:"
                echo "  --all     Run all checks (default)"
                echo "  --format  Run format check only"
                echo "  --lint    Run static analysis only"
                echo "  --test    Run unit tests only"
                exit 0
                ;;
            *)
                echo "Unknown option: $arg"
                exit 1
                ;;
        esac
    done

    echo ""
    echo "ACS ESP-Miner Pre-commit Checks"
    echo "==============================="

    # Run selected checks
    if $run_all || $run_format; then
        check_format || FAILED=1
    fi

    if $run_all; then
        check_whitespace || FAILED=1
    fi

    if $run_all || $run_lint; then
        check_lint || FAILED=1
    fi

    if $run_all || $run_test; then
        check_tests || FAILED=1
    fi

    # Final summary
    echo ""
    echo "==============================="
    if [ $FAILED -eq 0 ]; then
        print_success "All checks passed!"
        exit 0
    else
        print_error "Some checks failed!"
        exit 1
    fi
}

main "$@"
