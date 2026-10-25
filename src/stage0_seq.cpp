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

    Record rec{};
    
    // Допоміжна лямбда для відрізання наступного поля до пробілу
    auto next_token = [&line]() -> std::optional<std::string_view> {
        if (line.empty()) return std::nullopt;
        size_t space_pos = line.find(' ');
        std::string_view token = line.substr(0, space_pos);
        if (token.empty()) return std::nullopt; // Порожні поля заборонені
        if (space_pos == std::string_view::npos) {
            line = {}; // Це був останній токен
        } else {
            line = line.substr(space_pos + 1);
        }
        return token;
    };

    // 1. ts_ms
    auto ts_str = next_token();
    if (!ts_str || ts_str->front() == '+' || ts_str->front() == '-') return std::nullopt;
    auto res = std::from_chars(ts_str->data(), ts_str->data() + ts_str->size(), rec.ts_ms);
    if (res.ec != std::errc{} || res.ptr != ts_str->data() + ts_str->size()) return std::nullopt;

    // 2. ip (лише беремо значення, детально не валідуємо за п. 2.4)
    auto ip_str = next_token();
    if (!ip_str) return std::nullopt;

    // 3. method
    auto method_str = next_token();
    if (!method_str) return std::nullopt;
    if (*method_str != "GET" && *method_str != "POST" && *method_str != "PUT" && *method_str != "DELETE") return std::nullopt;

    // 4. path
    auto path_str = next_token();
    if (!path_str || path_str->empty() || path_str->front() != '/') return std::nullopt;
    rec.path = *path_str;

    // 5. status
    auto status_str = next_token();
    if (!status_str || status_str->front() == '+' || status_str->front() == '-') return std::nullopt;
    res = std::from_chars(status_str->data(), status_str->data() + status_str->size(), rec.status);
    if (res.ec != std::errc{} || res.ptr != status_str->data() + status_str->size()) return std::nullopt;
    if (rec.status < 100 || rec.status > 599) return std::nullopt;

    // 6. bytes
    auto bytes_str = next_token();
    if (!bytes_str || bytes_str->front() == '+' || bytes_str->front() == '-') return std::nullopt;
    res = std::from_chars(bytes_str->data(), bytes_str->data() + bytes_str->size(), rec.bytes);
    if (res.ec != std::errc{} || res.ptr != bytes_str->data() + bytes_str->size()) return std::nullopt;

    // 7. latency_ms
    auto latency_str = next_token();
    if (!latency_str || latency_str->front() == '+' || latency_str->front() == '-') return std::nullopt;
    res = std::from_chars(latency_str->data(), latency_str->data() + latency_str->size(), rec.latency_ms);
    if (res.ec != std::errc{} || res.ptr != latency_str->data() + latency_str->size()) return std::nullopt;
    if (rec.latency_ms > 9999) return std::nullopt;

    // Якщо залишились ще якісь дані (тобто полів більше 7), рядок пошкоджений
    if (!line.empty()) return std::nullopt;

    return rec;
}

int main(int argc, char* argv[]) {
    std::uint64_t lines = 100000;
    std::uint64_t seed = 2026;

    std::cout << "Generating " << lines << " lines...\n";
    std::string buffer = logan::generate_range(seed, 0, lines);
    std::cout << "Buffer size: " << buffer.size() << " bytes\n";

    return 0;
}