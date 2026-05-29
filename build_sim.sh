#!/bin/bash
# X-HEEP Verilator simulation build wrapper
# Handles Verilator 5.020 workaround for chandle trace type issue
set -e

BUILD_DIR="build/openhwgroup.org_systems_core-v-mini-mcu_1.0.5/sim-verilator"

echo "=== Building X-HEEP Verilator simulation ==="

# Run FuseSoC (may fail on trace generation step)
fusesoc --cores-root . run --target=sim --tool=verilator \
    openhwgroup.org:systems:core-v-mini-mcu 2>&1 || true

# Fix Verilator 5.020 bug: empty VerilatedTraceSigType for chandle signals
TRACE_FILE="$BUILD_DIR/Vtestharness__Trace__0__Slow.cpp"
if [ -f "$TRACE_FILE" ]; then
    if grep -q "VerilatedTraceSigType::," "$TRACE_FILE" 2>/dev/null; then
        echo "=== Applying chandle trace workaround ==="
        sed -i 's/VerilatedTraceSigType::,/VerilatedTraceSigType::LOGIC,/g' "$TRACE_FILE"
    fi
fi

# Complete the build
if [ -f "$BUILD_DIR/Vtestharness.mk" ]; then
    echo "=== Building Vtestharness binary ==="
    make -C "$BUILD_DIR" -f Vtestharness.mk
fi

if [ -f "$BUILD_DIR/Vtestharness" ]; then
    echo "=== Build successful! ==="
    echo "Binary: $BUILD_DIR/Vtestharness"
else
    echo "=== Build failed ==="
    exit 1
fi
