// generator.hpp — детермінований генератор журналу запитів.
#pragma once
#include <cstdint>
#include <string>

namespace logan {

class LineRng {
public:
    LineRng(std::uint64_t seed, std::uint64_t i)
        : s_(mix(seed) ^ (i * 0xD1B54A32D192ED03ull)) {}

    std::uint64_t next() { return mix(s_ += 0x9E3779B97F4A7C15ull); }
    std::uint32_t below(std::uint32_t n) {                // [0, n)
        return static_cast<std::uint32_t>(((next() >> 32) * n) >> 32);
    }
    std::uint32_t skewed(std::uint32_t n) {               // [0, n), малі значення частіші
        std::uint64_t x = next() >> 32;
        x = (((x * x) >> 32) * x) >> 32;                  // x^3 / 2^64
        return static_cast<std::uint32_t>((x * n) >> 32);
    }

private:
    static std::uint64_t mix(std::uint64_t z) {           // SplitMix64 finalizer
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    std::uint64_t s_;
};

// Дописує в out рядок номер i (разом із '\n').
inline void append_line(std::string& out, std::uint64_t seed, std::uint64_t i) {
    static constexpr const char* kMethods[] = {"GET", "POST", "PUT", "DELETE"};
    static constexpr const char* kRes[] = {"users",  "orders", "products", "carts",
                                           "search", "auth",   "payments", "reviews"};
    LineRng r(seed, i);

    if (r.below(1000) == 0) {                             // ~0,1 % пошкоджених рядків
        switch (r.below(3)) {
        case 0: out += "1788220800000 10.0.0.1 GET /api/v1/users/1 200 512\n"; return;
        case 1: out += "1788220800000 10.0.0.1 GET /api/v1/users/1 2x0 512 17\n"; return;
        default: out += "#### corrupted ####\n"; return;
        }
    }

    std::uint64_t ts = 1788220800000ull + i * 20 + r.below(20);   // від 2026-09-01
    std::uint32_t m = r.below(100);
    const char* method = m < 70 ? kMethods[0] : m < 90 ? kMethods[1] : m < 95 ? kMethods[2] : kMethods[3];

    std::uint32_t p = r.below(10000);
    int status = p < 8500 ? 200 : p < 9000 ? 201 : p < 9200 ? 204 : p < 9300 ? 301
               : p < 9500 ? 304 : p < 9650 ? 400 : p < 9700 ? 401 : p < 9750 ? 403
               : p < 9900 ? 404 : p < 9930 ? 429 : p < 9970 ? 500 : p < 9990 ? 502 : 503;
    std::uint32_t bytes = (status == 204 || status == 304) ? 0 : 200 + r.below(50000);

    std::uint32_t latency = 5 + r.below(40);
    std::uint32_t slow = r.below(1000);
    if (slow < 5)       latency = 2000 + r.below(7999);   // 0,5 % дуже повільних
    else if (slow < 55) latency += r.below(2000);         // 5 % повільних

    std::uint32_t res = r.skewed(8), id = r.skewed(1000);
    std::uint32_t ip = r.below(1u << 18);

    out += std::to_string(ts);
    out += " 10.";
    out += std::to_string(ip >> 16);  out += '.';
    out += std::to_string((ip >> 8) & 0xFF); out += '.';
    out += std::to_string(ip & 0xFF); out += ' ';
    out += method;
    out += " /api/v1/"; out += kRes[res]; out += '/'; out += std::to_string(id);
    out += ' '; out += std::to_string(status);
    out += ' '; out += std::to_string(bytes);
    out += ' '; out += std::to_string(latency);
    out += '\n';
}

// Генерує рядки [first, last) в один буфер.
inline std::string generate_range(std::uint64_t seed, std::uint64_t first, std::uint64_t last) {
    std::string out;
    out.reserve((last - first) * 72);
    for (std::uint64_t i = first; i < last; ++i) append_line(out, seed, i);
    return out;
}

} // namespace logan