#!/bin/bash
################################################################################
# build_stubs.sh - Build and test QCSQL service stubs on Linux/WSL/QEMU
# For Citadel OS porting - Option A, Step 1
################################################################################

set -e  # Exit on error

echo ""
echo "╔══════════════════════════════════════════════════╗"
echo "║  QCSQL Service Stub Builder (Linux/WSL/QEMU)    ║"
echo "║  Option A: Quick Testing - Step 1               ║"
echo "╚══════════════════════════════════════════════════╝"
echo ""

# Check for g++ compiler
if ! command -v g++ &> /dev/null; then
    echo "[ERROR] g++ compiler not found!"
    echo "Please install: sudo apt install build-essential"
    exit 1
fi

echo "[INFO] Found g++ compiler: $(g++ --version | head -n1)"
echo ""

# Create build directory
mkdir -p build_stubs
cd build_stubs

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo " Step 1: Compiling QCSQLService.cpp"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
g++ -c -std=c++17 -Wall -I.. ../QCSQLService.cpp -o QCSQLService.o
echo "[SUCCESS] QCSQLService.cpp compiled"
echo ""

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo " Step 2: Compiling QCSQLServiceTest.cpp"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
g++ -c -std=c++17 -Wall -I.. ../QCSQLServiceTest.cpp -o QCSQLServiceTest.o
echo "[SUCCESS] QCSQLServiceTest.cpp compiled"
echo ""

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo " Step 3: Linking test executable"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
g++ -o qcsql_test QCSQLService.o QCSQLServiceTest.o
echo "[SUCCESS] Linked qcsql_test"
echo ""

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo " Step 4: Running tests"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
./qcsql_test
echo ""

cd ..

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo " Build Summary"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo " Compiled: QCSQLService.cpp, QCSQLServiceTest.cpp"
echo " Linked:   build_stubs/qcsql_test"
echo " Tests:    Executed"
echo ""
echo " Note: All functionality is STUB mode"
echo "       Actual CQL engine not connected yet"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

echo "[INFO] Build artifacts in build_stubs/ directory"
echo "[INFO] Next step: Port Windows CQL engine (Phase 2)"
echo ""
