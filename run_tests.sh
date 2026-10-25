#!/bin/bash
echo "=== Збирання проєкту (Release) ==="
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

echo -e "\n=== Етап 0: Перевірка 100 000 рядків ==="
./build/stage0_seq --lines 100000 --seed 2026 --report | grep -E "checksum|total|ok|bad"

echo -e "\n=== Етап 0: Перевірка 1 000 000 рядків ==="
./build/stage0_seq --lines 1000000 --seed 2026 | grep -E "checksum|total|ok|bad"
