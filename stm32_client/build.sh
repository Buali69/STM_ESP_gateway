#!/bin/bash
set -e

NAME="stm32_client2"
OUT="/mnt/c/temp/stm32"

cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake

cmake --build build

mkdir -p "$OUT"

cp build/${NAME}.elf "$OUT/"
cp build/${NAME}.bin "$OUT/"
cp build/${NAME}.hex "$OUT/"
cp build/${NAME}.map "$OUT/"

echo "STM32 artifacts copied to $OUT"