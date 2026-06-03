#!/bin/bash
################################################################################
# build_stubs.sh - Build and test QCSQL service on Linux/WSL/QEMU
# Uses the imported standalone CQL engine backend.
################################################################################

set -e

echo ""
echo "╔══════════════════════════════════════════════════╗"
echo "║  QCSQL Service CQL Builder (Linux/WSL/QEMU)     ║"
echo "║  Service + imported engine validation           ║"
echo "╚══════════════════════════════════════════════════╝"
echo ""

if ! command -v g++ &> /dev/null; then
    echo "[ERROR] g++ compiler not found!"
    echo "Please install: sudo apt install build-essential"
    exit 1
fi

echo "[INFO] Found g++ compiler: $(g++ --version | head -n1)"
echo ""

mkdir -p build_stubs
cd build_stubs

COMMON_FLAGS=(-std=c++17 -Wall -Wextra -DCITADEL_QCSQL_USE_CQL_ENGINE -I.. -I../src/Storage)
ENGINE_SOURCES=(
    ../QCSQLService.cpp
    ../QCSQLServiceTest.cpp
    ../Database.cpp
    ../DiagnosticDumper.cpp
    ../FileManager.cpp
    ../PageManager.cpp
    ../QueryExecutor.cpp
    ../RowSerializer.cpp
    ../SQLParser.cpp
    ../Table.cpp
    ../WhereClauseEvaluator.cpp
    ../src/Storage/BTree.cpp
)

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo " Step 1: Compiling and linking service test"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
g++ "${COMMON_FLAGS[@]}" "${ENGINE_SOURCES[@]}" -o qcsql_test
echo "[SUCCESS] Linked qcsql_test with CQL engine sources"
echo ""

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo " Step 2: Running tests"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
./qcsql_test
echo ""

cd ..

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo " Build Summary"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo " Compiled: QCSQLService.cpp, QCSQLServiceTest.cpp, CQL engine sources"
echo " Linked:   build_stubs/qcsql_test"
echo " Tests:    Executed"
echo ""
echo " Note: Service test ran with CITADEL_QCSQL_USE_CQL_ENGINE enabled"
echo "       Imported standalone engine is connected"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

echo "[INFO] Build artifacts in build_stubs/ directory"
echo "[INFO] Next step: fold this into the main project build"
echo ""
