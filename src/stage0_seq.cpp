#include <iostream>
#include <string>
#include <string_view>
#include <charconv>
#include <optional>
#include <vector>
#include "logan/generator.hpp"

// Структура для розібраного рядка (п. 2.4)
struct Record {
    std::uint64_t ts_ms;
    std::string_view path;
    int status;
    std::uint32_t bytes;
    std::uint32_t latency_ms;
};

// Агрегатор статистики (п. 2.5)
struct Stats {
    std::uint64_t ok = 0;
    std::uint64_t bad = 0;
    std::uint64_t total = 0;
    std::uint64_t bytes = 0;
    
    void merge(const Stats& other) {
        ok += other.ok;
        bad += other.bad;
        total += other.total;
        bytes += other.bytes;
    }
};

std::optional<Record> parse_line(std::string_view line) {
    if (line.empty()) return std::nullopt;
    return std::nullopt; // Тимчасова заглушка
}

int main(int argc, char* argv[]) {
    std::uint64_t lines = 100000;
    std::uint64_t seed = 2026;

    std::cout << "Generating " << lines << " lines...\n";
    std::string buffer = logan::generate_range(seed, 0, lines);
    std::cout << "Buffer size: " << buffer.size() << " bytes\n";

    return 0;
}
