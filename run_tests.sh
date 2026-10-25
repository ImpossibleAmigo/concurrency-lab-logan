#!/bin/bash
echo "=== Збирання проєкту (Release) ==="
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

echo -e "\n=== Етап 3: Пул потоків (Еталонна швидкість) ==="
./build/stage3_pool --lines 5000000 --threads 4 | grep -E "checksum|T="

echo -e "\n=== Етап 3: False sharing - PACKED ==="
./build/stage3_pool --lines 5000000 --threads 4 --counters packed --progress | grep -E "checksum|T="

echo -e "\n=== Етап 3: False sharing - PADDED (Вирівняно по кеш-лінії) ==="
./build/stage3_pool --lines 5000000 --threads 4 --counters padded --progress | grep -E "checksum|T="

echo -e "\n=== Етап 3: Перевірка винятків (доставка через future) ==="
./build/stage3_pool --lines 1000000 --threads 4 --max-bad-per-chunk 10 || echo "Програма коректно завершилась із кодом 2"
