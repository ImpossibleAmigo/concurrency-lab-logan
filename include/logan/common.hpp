#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <optional>
#include <array>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <charconv>

namespace logan {

inline std::uint64_t checksum(std::string_view text) {
    std::uint64_t h = 0xCBF29CE484222325ull;
    for (unsigned char c : text) { h ^= c; h *= 0x100000001B3ull; }
    return h;
}

struct Record {
    std::uint64_t ts_ms;
    std::string_view ip;
    std::string_view method;
    std::string_view path;
    std::uint32_t status;
    std::uint32_t bytes;
    std::uint32_t latency_ms;
};

struct Stats {
    std::uint64_t ok = 0;
    std::uint64_t bad = 0;
    std::uint64_t total = 0;
    std::uint64_t bytes = 0;
    std::array<std::uint64_t, 600> statuses{};
    std::array<std::uint64_t, 10000> latencies{};
    std::unordered_map<std::string, std::uint64_t> paths;

    void add_bad() {
        bad++;
        total++;
    }

    void add_record(const Record& r) {
        ok++;
        total++;
        bytes += r.bytes;
        statuses[r.status]++;
        latencies[r.latency_ms]++;
        paths[std::string(r.path)]++;
    }

    void merge(const Stats& other) {
        ok += other.ok;
        bad += other.bad;
        total += other.total;
        bytes += other.bytes;
        for (std::size_t i = 0; i < statuses.size(); ++i) statuses[i] += other.statuses[i];
        for (std::size_t i = 0; i < latencies.size(); ++i) latencies[i] += other.latencies[i];
        for (const auto& [p, count] : other.paths) paths[p] += count;
    }
};

inline std::optional<Record> parse_line(std::string_view line) {
    if (line.empty()) return std::nullopt;
    if (line.back() == '\r') line.remove_suffix(1); 

    std::array<std::string_view, 7> fields;
    std::size_t start = 0;
    for (int i = 0; i < 7; ++i) {
        if (start >= line.size()) return std::nullopt;
        std::size_t space = line.find(' ', start);
        if (i < 6 && space == std::string_view::npos) return std::nullopt;
        if (i == 6 && space != std::string_view::npos) return std::nullopt; 
        
        std::size_t end = (space == std::string_view::npos) ? line.size() : space;
        if (start == end) return std::nullopt;
        fields[i] = line.substr(start, end - start);
        start = end + 1;
    }

    Record r;
    r.ip = fields[1];
    r.method = fields[2];
    r.path = fields[3];

    if (r.method != "GET" && r.method != "POST" && r.method != "PUT" && r.method != "DELETE") return std::nullopt;
    if (r.path.empty() || r.path[0] != '/') return std::nullopt;

    auto parse_uint = [](std::string_view s, auto& out) -> bool {
        if (s.empty()) return false;
        for (char c : s) if (c < '0' || c > '9') return false;
        auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
        return ec == std::errc() && ptr == s.data() + s.size();
    };

    if (!parse_uint(fields[0], r.ts_ms)) return std::nullopt;
    if (!parse_uint(fields[4], r.status) || r.status < 100 || r.status > 599) return std::nullopt;
    if (!parse_uint(fields[5], r.bytes)) return std::nullopt;
    if (!parse_uint(fields[6], r.latency_ms) || r.latency_ms > 9999) return std::nullopt;

    return r;
}

inline std::string generate_report(const Stats& s) {
    std::string out;
    out += "total=" + std::to_string(s.total) + "\n";
    out += "ok=" + std::to_string(s.ok) + "\n";
    out += "bad=" + std::to_string(s.bad) + "\n";
    out += "bytes=" + std::to_string(s.bytes) + "\n";

    for (std::size_t i = 100; i <= 599; ++i) {
        if (s.statuses[i] > 0) {
            out += "status " + std::to_string(i) + "=" + std::to_string(s.statuses[i]) + "\n";
        }
    }

    auto get_percentile = [&](double p) -> std::uint32_t {
        if (s.ok == 0) return 0;
        std::uint64_t rank = std::max<std::uint64_t>(1, (s.ok * p + 99) / 100);
        std::uint64_t count = 0;
        for (std::uint32_t i = 0; i < s.latencies.size(); ++i) {
            count += s.latencies[i];
            if (count >= rank) return i;
        }
        return 0;
    } ;

    out += "p50=" + std::to_string(get_percentile(50)) + "\n";
    out += "p95=" + std::to_string(get_percentile(95)) + "\n";
    out += "p99=" + std::to_string(get_percentile(99)) + "\n";

    out += "paths=" + std::to_string(s.paths.size()) + "\n";

    using PathCount = std::pair<std::string, std::uint64_t>;
    std::vector<PathCount> top_paths(s.paths.begin(), s.paths.end());
    std::sort(top_paths.begin(), top_paths.end(), [](const PathCount& a, const PathCount& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });

    std::size_t limit = std::min<std::size_t>(10, top_paths.size());
    for (std::size_t i = 0; i < limit; ++i) {
        out += "top" + std::to_string(i + 1) + " " + top_paths[i].first + " " + std::to_string(top_paths[i].second) + "\n";
    }

    return out;
}

} // namespace logan