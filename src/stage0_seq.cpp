#include <iostream>
#include <string>
#include <string_view>
#include <charconv>
#include <optional>
#include <vector>
#include <array>
#include <unordered_map>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <chrono>
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
    
    std::array<std::uint32_t, 600> statuses{};
    std::array<std::uint32_t, 10000> latencies{};
    std::unordered_map<std::string_view, std::uint32_t> paths;
    
    void merge(const Stats& other) {
        ok += other.ok;
        bad += other.bad;
        total += other.total;
        bytes += other.bytes;
        
        for (size_t i = 100; i < statuses.size(); ++i) {
            statuses[i] += other.statuses[i];
        }
        for (size_t i = 0; i < latencies.size(); ++i) {
            latencies[i] += other.latencies[i];
        }
        for (const auto& [path, count] : other.paths) {
            paths[path] += count;
        }
    }
};

std::optional<Record> parse_line(std::string_view line) {
    if (line.empty()) return std::nullopt;

    Record rec{};
    auto next_token = [&line]() -> std::optional<std::string_view> {
        if (line.empty()) return std::nullopt;
        size_t space_pos = line.find(' ');
        std::string_view token = line.substr(0, space_pos);
        if (token.empty()) return std::nullopt; 
        if (space_pos == std::string_view::npos) line = {}; 
        else line = line.substr(space_pos + 1);
        return token;
    };

    auto ts_str = next_token();
    if (!ts_str || ts_str->front() == '+' || ts_str->front() == '-') return std::nullopt;
    auto res = std::from_chars(ts_str->data(), ts_str->data() + ts_str->size(), rec.ts_ms);
    if (res.ec != std::errc{} || res.ptr != ts_str->data() + ts_str->size()) return std::nullopt;

    auto ip_str = next_token();
    if (!ip_str) return std::nullopt;

    auto method_str = next_token();
    if (!method_str) return std::nullopt;
    if (*method_str != "GET" && *method_str != "POST" && *method_str != "PUT" && *method_str != "DELETE") return std::nullopt;

    auto path_str = next_token();
    if (!path_str || path_str->empty() || path_str->front() != '/') return std::nullopt;
    rec.path = *path_str;

    auto status_str = next_token();
    if (!status_str || status_str->front() == '+' || status_str->front() == '-') return std::nullopt;
    res = std::from_chars(status_str->data(), status_str->data() + status_str->size(), rec.status);
    if (res.ec != std::errc{} || res.ptr != status_str->data() + status_str->size()) return std::nullopt;
    if (rec.status < 100 || rec.status > 599) return std::nullopt;

    auto bytes_str = next_token();
    if (!bytes_str || bytes_str->front() == '+' || bytes_str->front() == '-') return std::nullopt;
    res = std::from_chars(bytes_str->data(), bytes_str->data() + bytes_str->size(), rec.bytes);
    if (res.ec != std::errc{} || res.ptr != bytes_str->data() + bytes_str->size()) return std::nullopt;

    auto latency_str = next_token();
    if (!latency_str || latency_str->front() == '+' || latency_str->front() == '-') return std::nullopt;
    res = std::from_chars(latency_str->data(), latency_str->data() + latency_str->size(), rec.latency_ms);
    if (res.ec != std::errc{} || res.ptr != latency_str->data() + latency_str->size()) return std::nullopt;
    if (rec.latency_ms > 9999) return std::nullopt;

    if (!line.empty()) return std::nullopt;

    return rec;
}

// Цілочисельний підрахунок перцентилів
std::uint32_t calc_percentile(const std::array<std::uint32_t, 10000>& latencies, std::uint64_t ok, std::uint64_t p) {
    if (ok == 0) return 0;
    std::uint64_t rank = std::max<std::uint64_t>(1, (ok * p + 99) / 100);
    std::uint64_t sum = 0;
    for (size_t i = 0; i < latencies.size(); ++i) {
        sum += latencies[i];
        if (sum >= rank) return static_cast<std::uint32_t>(i);
    }
    return 0;
}

// Контрольна сума FNV-1a
std::uint64_t checksum(std::string_view text) {
    std::uint64_t h = 0xCBF29CE484222325ull;
    for (unsigned char c : text) { 
        h ^= c; 
        h *= 0x100000001B3ull; 
    }
    return h;
}

int main(int argc, char* argv[]) {
    std::uint64_t lines = 5000000;
    std::uint64_t seed = 2026;
    bool print_report = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--lines" && i + 1 < argc) lines = std::stoull(argv[++i]);
        else if (arg == "--seed" && i + 1 < argc) seed = std::stoull(argv[++i]);
        else if (arg == "--report") print_report = true;
    }

    auto start_gen = std::chrono::steady_clock::now();
    std::string buffer = logan::generate_range(seed, 0, lines);
    auto end_gen = std::chrono::steady_clock::now();

    auto start_proc = std::chrono::steady_clock::now();
    Stats stats;
    size_t start_pos = 0;
    while (start_pos < buffer.size()) {
        size_t end_pos = buffer.find('\n', start_pos);
        if (end_pos == std::string::npos) end_pos = buffer.size();
        
        std::string_view line(buffer.data() + start_pos, end_pos - start_pos);
        stats.total++;
        auto rec = parse_line(line);
        if (rec) {
            stats.ok++;
            stats.bytes += rec->bytes;
            stats.statuses[rec->status]++;
            stats.latencies[rec->latency_ms]++;
            stats.paths[rec->path]++;
        } else {
            stats.bad++;
        }
        start_pos = end_pos + 1;
    }
    auto end_proc = std::chrono::steady_clock::now();

    auto start_rep = std::chrono::steady_clock::now();
    std::vector<std::pair<std::string_view, std::uint32_t>> sorted_paths(stats.paths.begin(), stats.paths.end());
    std::sort(sorted_paths.begin(), sorted_paths.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });

    std::ostringstream report;
    report << "total=" << stats.total << '\n'
           << "ok=" << stats.ok << '\n'
           << "bad=" << stats.bad << '\n'
           << "bytes=" << stats.bytes << '\n';
    
    for (size_t i = 100; i <= 599; ++i) {
        if (stats.statuses[i] > 0) report << "status " << i << "=" << stats.statuses[i] << '\n';
    }
    
    report << "p50=" << calc_percentile(stats.latencies, stats.ok, 50) << '\n'
           << "p95=" << calc_percentile(stats.latencies, stats.ok, 95) << '\n'
           << "p99=" << calc_percentile(stats.latencies, stats.ok, 99) << '\n'
           << "paths=" << stats.paths.size() << '\n';
           
    for (size_t i = 0; i < std::min<size_t>(10, sorted_paths.size()); ++i) {
        report << "top" << (i + 1) << " " << sorted_paths[i].first << " " << sorted_paths[i].second << '\n';
    }
    
    std::string report_str = report.str();
    std::uint64_t csum = checksum(report_str);
    auto end_rep = std::chrono::steady_clock::now();

    if (print_report) std::cout << report_str;

    double t_gen = std::chrono::duration<double, std::milli>(end_gen - start_gen).count();
    double t_proc = std::chrono::duration<double, std::milli>(end_proc - start_proc).count();
    double t_rep = std::chrono::duration<double, std::milli>(end_rep - start_rep).count();

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "T_gen=" << t_gen << " ms  T_proc=" << t_proc << " ms  T_rep=" << t_rep << " ms  ";
    std::cout << "checksum=" << std::hex << std::setw(16) << std::setfill('0') << csum << std::dec << '\n';

    return 0;
}