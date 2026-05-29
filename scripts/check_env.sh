#!/bin/bash

# X-HEEP 工具链自检脚本
# 用法: bash scripts/check_env.sh

check() {
    if command -v "$1" &> /dev/null; then
        echo "✓ $1: $(command -v $1)"
    else
        echo "✗ $1: NOT FOUND"
    fi
}

echo "=== X-HEEP 工具链检查 ==="
echo ""

check riscv-none-elf-gcc
check verilator
check gtkwave
check fusesoc
check cmake
check ninja
check python3

echo ""
echo "=== 版本信息 ==="
riscv-none-elf-gcc --version 2>/dev/null | head -1
verilator --version 2>/dev/null
gtkwave --version 2>/dev/null | head -1
fusesoc --version 2>/dev/null
cmake --version 2>/dev/null | head -1

echo ""
echo "=== GCC Multilib ==="
riscv-none-elf-gcc -print-multi-lib 2>/dev/null

echo ""
echo "=== 环境变量 ==="
echo "RISCV_XHEEP       = ${RISCV_XHEEP:-未设置}"
echo "COMPILER_PREFIX   = ${COMPILER_PREFIX:-未设置}"
echo "COMPILER          = ${COMPILER:-未设置}"
echo "ARCH              = ${ARCH:-未设置}"
