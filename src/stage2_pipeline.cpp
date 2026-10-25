#include "logan/common.hpp"
#include "logan/generator.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

using namespace logan;
using namespace std::chrono;

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {}

    bool push(T value) {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_push_.wait(lock, [this]() { return closed_ || capacity_ == 0 || q_.size() < capacity_; });
        if (closed_) return false;
        q_.push(std::move(value));
        cv_pop_.notify_one();
        return true;
    }

    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_pop_.wait(lock, [this]() { return closed_ || !q_.empty(); });
        if (closed_ && q_.empty()) return std::nullopt;
        T val = std::move(q_.front());
        q_.pop();
        cv_push_.notify_one();
        return val;
    }

    void close() {
        std::lock_guard<std::mutex> lock(mtx_);
        closed_ = true;
        cv_push_.notify_all();
        cv_pop_.notify_all();
    }

private:
    std::size_t capacity_;
    std::queue<T> q_;
    std::mutex mtx_;
    std::condition_variable cv_push_;
    std::condition_variable cv_pop_;
    bool closed_ = false;
};

struct Chunk {
    int id;
    std::string data;
};

int main(int argc, char* argv[]) {
    uint64_t lines = 5000000;
    uint64_t seed = 2026;
    int workers_count = 1;
    int chunk_lines = 10000;
    std::string mode = "owned";
    int fail_chunk = -1;
    size_t cap = 0;
    int slow_us = 0;
    bool report = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--lines" && i + 1 < argc) lines = std::stoull(argv[++i]);
        else if (arg == "--seed" && i + 1 < argc) seed = std::stoull(argv[++i]);
        else if (arg == "--workers" && i + 1 < argc) workers_count = std::stoi(argv[++i]);
        else if (arg == "--chunk-lines" && i + 1 < argc) chunk_lines = std::stoi(argv[++i]);
        else if (arg == "--mode" && i + 1 < argc) mode = argv[++i];
        else if (arg == "--fail-chunk" && i + 1 < argc) fail_chunk = std::stoi(argv[++i]);
        else if (arg == "--cap" && i + 1 < argc) cap = std::stoull(argv[++i]);
        else if (arg == "--slow-us" && i + 1 < argc) slow_us = std::stoi(argv[++i]);
        else if (arg == "--report") report = true;
    }

    auto t0 = steady_clock::now();

    BoundedQueue<Chunk> queue(cap);
    Stats global_stats;
    std::mutex global_mutex;
    std::atomic<bool> has_error{false};

    std::jthread producer([&]() {
        int chunk_id = 0;
        for (uint64_t i = 0; i < lines; i += chunk_lines) {
            uint64_t end = std::min(i + (uint64_t)chunk_lines, lines);
            Chunk c{chunk_id++, generate_range(seed, i, end)};
            if (!queue.push(std::move(c))) break;
        }
        queue.close();
    });

    std::vector<std::jthread> consumers;
    for (int w = 0; w < workers_count; ++w) {
        consumers.emplace_back([&, w]() {
            try {
                Stats owned_stats;
                while (auto chunk_opt = queue.pop()) {
                    auto& chunk = *chunk_opt;
                    if (fail_chunk >= 0 && chunk.id == fail_chunk) {
                        throw std::runtime_error("Simulated failure in chunk " + std::to_string(chunk.id));
                    }
                    if (slow_us > 0) std::this_thread::sleep_for(std::chrono::microseconds(slow_us));

                    Stats chunk_stats;
                    size_t start = 0;
                    while (start < chunk.data.size()) {
                        size_t end = chunk.data.find('\n', start);
                        if (end == std::string_view::npos) end = chunk.data.size();
                        std::string_view line(chunk.data.data() + start, end - start);
                        auto record = parse_line(line);

                        if (mode == "global") {
                            std::scoped_lock lock(global_mutex);
                            if (record) global_stats.add_record(*record);
                            else global_stats.add_bad();
                        } else if (mode == "chunk") {
                            if (record) chunk_stats.add_record(*record);
                            else chunk_stats.add_bad();
                        } else if (mode == "owned") {
                            if (record) owned_stats.add_record(*record);
                            else owned_stats.add_bad();
                        }
                        start = end + 1;
                    }

                    if (mode == "chunk") {
                        std::scoped_lock lock(global_mutex);
                        global_stats.merge(chunk_stats);
                    }
                }
                if (mode == "owned") {
                    std::scoped_lock lock(global_mutex);
                    global_stats.merge(owned_stats);
                }
            } catch (const std::exception& e) {
                std::cerr << "Worker " << w << " failed: " << e.what() << "\n";
                has_error = true;
                queue.close();
            }
        });
    }

    producer.join();
    for (auto& c : consumers) { if (c.joinable()) c.join(); }

    auto t1 = steady_clock::now();

    if (has_error) return 1;

    std::string rep = generate_report(global_stats);
    uint64_t chk = checksum(rep);
    
    if (report) std::cout << rep;

    double t_total = duration<double, std::milli>(t1 - t0).count();
    std::cout << "W=" << workers_count << " mode=" << mode 
              << " end-to-end=" << std::fixed << std::setprecision(2) << t_total 
              << " ms checksum=" << std::hex << std::setw(16) << std::setfill('0') << chk << std::dec << "\n";

    return 0;
}
