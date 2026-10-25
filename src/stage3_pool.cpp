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
#include <future>
#include <functional>
#include <memory>

using namespace logan;
using namespace std::chrono;

class ThreadPool {
    std::vector<std::jthread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable cv;
    bool stop = false;

public:
    explicit ThreadPool(unsigned threads) {
        for (unsigned i = 0; i < threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->cv.wait(lock, [this] { return this->stop || !this->tasks.empty(); });
                        if (this->stop && this->tasks.empty()) return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                }
            });
        }
    }

    template<class F>
    auto submit(F&& f) {
        using return_type = decltype(f());
        auto task = std::make_shared<std::packaged_task<return_type()>>(std::forward<F>(f));
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if (stop) throw std::runtime_error("submit on stopped ThreadPool");
            tasks.emplace([task]() { (*task)(); });
        }
        cv.notify_one();
        return res;
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        cv.notify_all();
        for (auto& worker : workers) {
            if (worker.joinable()) worker.join();
        }
    }
};

struct PackedCounter { std::atomic<uint64_t> v{0}; };
struct alignas(64) PaddedCounter { std::atomic<uint64_t> v{0}; };

int main(int argc, char* argv[]) {
    uint64_t lines = 5000000;
    uint64_t seed = 2026;
    int threads_count = 1;
    int chunk_lines = 50000;
    int max_bad = -1;
    bool progress = false;
    std::string counters_mode = "none";
    bool report = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--lines" && i + 1 < argc) lines = std::stoull(argv[++i]);
        else if (arg == "--seed" && i + 1 < argc) seed = std::stoull(argv[++i]);
        else if (arg == "--threads" && i + 1 < argc) threads_count = std::stoi(argv[++i]);
        else if (arg == "--chunk-lines" && i + 1 < argc) chunk_lines = std::stoi(argv[++i]);
        else if (arg == "--max-bad-per-chunk" && i + 1 < argc) max_bad = std::stoi(argv[++i]);
        else if (arg == "--progress") progress = true;
        else if (arg == "--counters" && i + 1 < argc) counters_mode = argv[++i];
        else if (arg == "--report") report = true;
    }

    auto t0 = steady_clock::now();

    ThreadPool pool(threads_count);
    Stats global_stats;
    std::queue<std::future<Stats>> in_flight;
    std::stop_source abort_source;
    std::stop_token stoken = abort_source.get_token();

    std::vector<PackedCounter> packed_counters(threads_count);
    std::vector<PaddedCounter> padded_counters(threads_count);
    std::atomic<uint64_t> current_task_id{0};

    std::jthread progress_thread;
    if (progress) {
        progress_thread = std::jthread([&](std::stop_token pt) {
            while (!pt.stop_requested()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                uint64_t total = 0;
                if (counters_mode == "packed") {
                    for (int i = 0; i < threads_count; ++i) total += packed_counters[i].v.load(std::memory_order_relaxed);
                } else if (counters_mode == "padded") {
                    for (int i = 0; i < threads_count; ++i) total += padded_counters[i].v.load(std::memory_order_relaxed);
                }
                if (counters_mode != "none") std::cerr << "\rProgress: " << total << " lines processed." << std::flush;
            }
            std::cerr << "\n";
        });
    }

    auto process_future = [&](std::future<Stats>& fut) {
        try {
            global_stats.merge(fut.get());
        } catch (const std::exception& e) {
            std::cerr << "\nError: " << e.what() << "\n";
            abort_source.request_stop();
            return false;
        }
        return true;
    };

    bool has_error = false;
    for (uint64_t i = 0; i < lines; i += chunk_lines) {
        if (abort_source.stop_requested()) {
            has_error = true;
            break;
        }

        uint64_t end_line = std::min(i + (uint64_t)chunk_lines, lines);
        
        while (in_flight.size() >= (size_t)(2 * threads_count)) {
            if (!process_future(in_flight.front())) { has_error = true; break; }
            in_flight.pop();
        }
        if (has_error) break;

        in_flight.push(pool.submit([=, &packed_counters, &padded_counters, &current_task_id, st = stoken]() {
            if (st.stop_requested()) return Stats{};
            int thread_idx = current_task_id.fetch_add(1, std::memory_order_relaxed) % threads_count;
            
            std::string buffer = generate_range(seed, i, end_line);
            Stats local_stats;
            size_t start = 0;
            
            while (start < buffer.size()) {
                size_t end = buffer.find('\n', start);
                if (end == std::string_view::npos) end = buffer.size();
                std::string_view line(buffer.data() + start, end - start);
                
                auto record = parse_line(line);
                if (record) local_stats.add_record(*record);
                else local_stats.add_bad();
                
                if (counters_mode == "packed") packed_counters[thread_idx].v.fetch_add(1, std::memory_order_relaxed);
                else if (counters_mode == "padded") padded_counters[thread_idx].v.fetch_add(1, std::memory_order_relaxed);

                start = end + 1;
            }

            if (max_bad >= 0 && local_stats.bad > (uint64_t)max_bad) {
                throw std::runtime_error("Too many bad lines in range [" + std::to_string(i) + ", " + std::to_string(end_line) + ")");
            }
            return local_stats;
        }));
    }

    while (!in_flight.empty()) {
        if (!has_error && !process_future(in_flight.front())) has_error = true;
        in_flight.pop();
    }

    if (progress_thread.joinable()) progress_thread.request_stop();

    auto t1 = steady_clock::now();

    if (has_error) return 2;

    std::string rep = generate_report(global_stats);
    uint64_t chk = checksum(rep);
    
    if (report) std::cout << rep;

    double t_total = duration<double, std::milli>(t1 - t0).count();
    std::cout << "T=" << threads_count << " end-to-end=" << std::fixed << std::setprecision(2) 
              << t_total << " ms checksum=" << std::hex << std::setw(16) << std::setfill('0') << chk << std::dec << "\n";

    return 0;
}
