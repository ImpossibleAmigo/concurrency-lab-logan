#!/bin/bash
echo "=== Збирання проєкту (Release) ==="
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

echo -e "\n=== Етап 1: Режим LOCAL (Без спільного стану - Еталон) ==="
./build/stage1_threads --lines 1000000 --threads 4 --mode local | grep -E "checksum|T="

echo -e "\n=== Етап 1: Режим ATOMIC (Коректно, але повільніше) ==="
./build/stage1_threads --lines 1000000 --threads 4 --mode atomic | grep -E "checksum|T="

echo -e "\n=== Етап 1: Режим RACE (Навмисна помилка - сума має 'попливти') ==="
./build/stage1_threads --lines 1000000 --threads 4 --mode race | grep -E "checksum|T="

echo -e "\n=== Етап 1: Перевірка скасування (Deadline 10ms) ==="
./build/stage1_threads --lines 5000000 --threads 4 --mode local --deadline-ms 10 | grep -E "PARTIAL|checksum|T="
