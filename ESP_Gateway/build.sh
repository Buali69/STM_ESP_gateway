#!/bin/bash
set -e

source ~/esp-idf/export.sh

OUT="/mnt/c/temp/esp32"

#idf.py set-target esp32
idf.py build

mkdir -p "$OUT"

cp build/*.bin "$OUT/" 2>/dev/null || true
cp build/bootloader/bootloader.bin "$OUT/"
cp build/partition_table/partition-table.bin "$OUT/"
cp build/*.elf "$OUT/" 2>/dev/null || true
cp build/*.map "$OUT/" 2>/dev/null || true

echo "ESP32 artifacts copied to $OUT"