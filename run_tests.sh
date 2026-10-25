#!/bin/bash
echo "=== Збирання проєкту (Release) ==="
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

echo -e "\n=== Етап 2: Режим OWNED (Найшвидший варіант) ==="
./build/stage2_pipeline --lines 1000000 --workers 4 --mode owned | grep -E "checksum|W="

echo -e "\n=== Етап 2: Режим CHUNK (Проміжні результати) ==="
./build/stage2_pipeline --lines 1000000 --workers 4 --mode chunk | grep -E "checksum|W="

echo -e "\n=== Етап 2: Режим GLOBAL (Найбільше блокувань) ==="
./build/stage2_pipeline --lines 1000000 --workers 4 --mode global | grep -E "checksum|W="

echo -e "\n=== Етап 2: Перевірка помилки (fail-chunk 5) ==="
./build/stage2_pipeline --lines 1000000 --workers 2 --fail-chunk 5 || echo "Програма коректно завершилась із кодом 1 (без зависання)"
