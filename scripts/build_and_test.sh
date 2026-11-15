#!/usr/bin/env bash
# Builds NanoBook and runs every test WITHOUT needing CMake (only a C++17 compiler).
#
# Usage, from the project folder:
#     bash scripts/build_and_test.sh
#
# Result: programs in build_simple/ (nanobook, nanobook_bench) and a PASS/FAIL line per test suite.
# Windows: run it inside an "MSYS2 UCRT64" terminal (or Git Bash with g++ on the PATH).

set -e
cd "$(dirname "$0")/.."

CXX=${CXX:-g++}
FLAGS="-std=c++17 -O2 -Wall -Wextra -Iinclude"
LIB="src/order_book.cpp src/csv_io.cpp src/workload.cpp src/snapshot_log.cpp"

mkdir -p build_simple

echo "Compiler: $($CXX --version | head -n 1)"
echo
echo "Building the programs..."
$CXX $FLAGS $LIB src/main.cpp  -o build_simple/nanobook
$CXX $FLAGS $LIB src/bench.cpp -o build_simple/nanobook_bench
echo "  built build_simple/nanobook and build_simple/nanobook_bench"

echo
echo "Building the tests..."
for t in tests/test_*.cpp; do
    name=$(basename "$t" .cpp)
    $CXX $FLAGS $LIB "$t" -o "build_simple/$name"
    echo "  built $name"
done

echo
echo "Running the tests..."
failed=0
for t in tests/test_*.cpp; do
    name=$(basename "$t" .cpp)
    if "./build_simple/$name" > "build_simple/$name.log" 2>&1; then
        echo "  PASS  $name   ($(tail -n 1 "build_simple/$name.log"))"
    else
        echo "  FAIL  $name   (details: build_simple/$name.log)"
        failed=1
    fi
done

echo
if [ "$failed" -eq 0 ]; then
    echo "All test suites passed."
else
    echo "Some test suites FAILED."
fi
exit $failed
