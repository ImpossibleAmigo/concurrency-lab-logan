#include "logan/common.hpp"
#include "logan/generator.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <stop_token>

using namespace logan;
using namespace std::chrono;

std::vector<std::string_view> split_buffer(std::string_view buf, int num_parts) {
    if (num_parts <= 1 || buf.empty()) {
        return {buf};
    }

    std::vector<size_t> lines;
    size_t pos = 0;
    while (pos < buf.size()) {
        lines.push_back(pos);
        size_t newline = buf.find('\n', pos);
        if (newline == std::string_view::npos) break;
        pos = newline + 1;
    }

    const size_t total_lines = lines.size();
    std::vector<std::string_view> parts;
    parts.reserve(num_parts);

    for (int i = 0; i < num_parts; ++i) {
        const size_t begin_index = (i * total_lines) / num_parts;
        const size_t end_index = ((i + 1) * total_lines) / num_parts;

        const size_t start = lines[begin_index];
        const size_t end = (end_index < total_lines) ? lines[end_index] : buf.size();
        parts.emplace_back(buf.data() + start, end - start);
    }

    return parts;
}

int main(int argc, char* argv[]) {
    uint64_t lines = 5000000;
    uint64_t seed = 2026;
    int threads = 1;
    std::string mode = "local";
    int deadline_ms = 0;
    bool report = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--lines" && i + 1 < argc) lines = std::stoull(argv[++i]);
        else if (arg == "--seed" && i + 1 < argc) seed = std::stoull(argv[++i]);
        else if (arg == "--threads" && i + 1 < argc) threads = std::stoi(argv[++i]);
        else if (arg == "--mode" && i + 1 < argc) mode = argv[++i];
        else if (arg == "--deadline-ms" && i + 1 < argc) deadline_ms = std::stoi(argv[++i]);
        else if (arg == "--report") report = true;
    }

    threads = std::max(1, threads);

    auto t0 = steady_clock::now();
    std::string buffer = generate_range(seed, 0, lines);
    auto t1 = steady_clock::now();

    auto parts = split_buffer(buffer, threads);
    Stats global_stats;

    std::atomic<uint64_t> race_ok{0}, race_bad{0}, race_bytes{0};
    std::atomic<uint64_t> atomic_ok{0}, atomic_bad{0}, atomic_bytes{0};
    std::mutex merge_mutex;

    std::vector<std::jthread> workers;
    workers.reserve(parts.size());

    for (size_t i = 0; i < parts.size(); ++i) {
        workers.emplace_back([&, i](std::stop_token stoken) {
            std::string_view chunk = parts[i];
            Stats local_stats;
            size_t start = 0;

            while (start < chunk.size()) {
                if (stoken.stop_requested()) break;

                size_t end = chunk.find('\n', start);
                if (end == std::string_view::npos) end = chunk.size();

                std::string_view line = chunk.substr(start, end - start);
                auto record = parse_line(line);

                if (mode == "local") {
                    if (record) local_stats.add_record(*record);
                    else local_stats.add_bad();
                } else if (mode == "race") {
                    if (record) {
                        race_ok.fetch_add(1, std::memory_order_relaxed);
                        race_bytes.fetch_add(record->bytes, std::memory_order_relaxed);
                        local_stats.add_record(*record);
                    } else {
                        race_bad.fetch_add(1, std::memory_order_relaxed);
                        local_stats.add_bad();
                    }
                } else if (mode == "atomic") {
                    if (record) {
                        atomic_ok.fetch_add(1, std::memory_order_relaxed);
                        atomic_bytes.fetch_add(record->bytes, std::memory_order_relaxed);
                        local_stats.add_record(*record);
                    } else {
                        atomic_bad.fetch_add(1, std::memory_order_relaxed);
                        local_stats.add_bad();
                    }
                }

                start = end + 1;
            }

            std::scoped_lock lock(merge_mutex);
            global_stats.merge(local_stats);
        });
    }

    bool partial = false;
    if (deadline_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(deadline_ms));
        for (auto& w : workers) w.request_stop();
        partial = true;
    }

    workers.clear();
    auto t2 = steady_clock::now();

    if (mode == "race") {
        global_stats.ok = race_ok.load(std::memory_order_relaxed);
        global_stats.bad = race_bad.load(std::memory_order_relaxed);
        global_stats.total = global_stats.ok + global_stats.bad;
        global_stats.bytes = race_bytes.load(std::memory_order_relaxed);
    } else if (mode == "atomic") {
        global_stats.ok = atomic_ok.load(std::memory_order_relaxed);
        global_stats.bad = atomic_bad.load(std::memory_order_relaxed);
        global_stats.total = global_stats.ok + global_stats.bad;
        global_stats.bytes = atomic_bytes.load(std::memory_order_relaxed);
    }

    std::string rep = generate_report(global_stats);
    uint64_t chk = checksum(rep);
    auto t3 = steady_clock::now();

    if (partial) std::cout << "PARTIAL\n";
    if (report) std::cout << rep;

    double t_total = duration<double, std::milli>(t3 - t0).count();
    double t_proc = duration<double, std::milli>(t2 - t1).count();

    std::cout << "T=" << threads << " mode=" << mode << " process=" << std::fixed << std::setprecision(2) << t_proc
              << " ms end-to-end=" << t_total << " ms checksum="
              << std::hex << std::setw(16) << std::setfill('0') << chk << std::dec << "\n";

    return 0;
}