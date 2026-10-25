#include "logan/common.hpp"
#include "logan/generator.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>

using namespace logan;
using namespace std::chrono;

int main(int argc, char* argv[]) {
    uint64_t lines = 5000000;
    uint64_t seed = 2026;
    bool report = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--lines" && i + 1 < argc) lines = std::stoull(argv[++i]);
        else if (arg == "--seed" && i + 1 < argc) seed = std::stoull(argv[++i]);
        else if (arg == "--report") report = true;
    }

    auto t0 = steady_clock::now();
    std::string buffer = generate_range(seed, 0, lines);
    auto t1 = steady_clock::now();

    Stats stats;
    std::string_view buf_view = buffer;
    
    size_t start = 0;
    while (start < buf_view.size()) {
        size_t end = buf_view.find('\n', start);
        if (end == std::string_view::npos) end = buf_view.size();
        
        std::string_view line = buf_view.substr(start, end - start);
        auto record = parse_line(line);
        if (record) stats.add_record(*record);
        else stats.add_bad();
        
        start = end + 1;
    }
    auto t2 = steady_clock::now();

    std::string rep = generate_report(stats);
    uint64_t chk = checksum(rep);
    auto t3 = steady_clock::now();

    if (report) std::cout << rep;

    double t_proc = duration<double, std::milli>(t2 - t1).count();
    double t_total = duration<double, std::milli>(t3 - t0).count();

    std::cout << "T=1   process=  " << std::fixed << std::setprecision(2) << t_proc 
              << " ms  speedup=1.00  end-to-end= " << t_total << " ms  checksum=" 
              << std::hex << std::setw(16) << std::setfill('0') << chk << std::dec << "\n";

    return 0;
}