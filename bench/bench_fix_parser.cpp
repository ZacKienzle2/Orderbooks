#include <lob/fix_parser.hpp>
#include <lob/messages.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>

#include <benchmark/benchmark.h>

namespace {

constexpr char k_soh = '\x01';

// Assemble a complete FIX 4.4 message from its body, prepending BeginString
// and BodyLength and appending a correct CheckSum. Same construction the unit
// tests use; runs once at static-init time so the timed loop measures parse()
// alone.
std::string make_fix(std::string_view body) {
    std::string m = "8=FIX.4.4";
    m += k_soh;
    m += "9=";
    m += std::to_string(body.size());
    m += k_soh;
    m += body;
    unsigned sum = 0;
    for (const char c : m)
        sum += static_cast<unsigned char>(c);
    sum %= 256U;
    char checksum[8];
    std::snprintf(checksum, sizeof(checksum), "10=%03u%c", sum, k_soh);
    m += checksum;
    return m;
}

const std::string g_new_order_single = make_fix("35=D\x01"
                                                "11=1001\x01"
                                                "55=AAPL\x01"
                                                "54=1\x01"
                                                "38=50\x01"
                                                "40=2\x01"
                                                "44=8192\x01"
                                                "59=1\x01");

const std::string g_cancel = make_fix("35=F\x01"
                                      "11=1002\x01"
                                      "41=1001\x01"
                                      "55=AAPL\x01"
                                      "54=1\x01");

std::span<const std::byte> bytes_of(const std::string& s) noexcept {
    return {reinterpret_cast<const std::byte*>(s.data()), s.size()};
}

void bench_parse_new_order_single(benchmark::State& state) {
    const auto buf = bytes_of(g_new_order_single);
    for (auto _ : state) {
        auto r = lob::fix::parse(buf);
        benchmark::DoNotOptimize(r);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() *
                            static_cast<std::int64_t>(g_new_order_single.size()));
}

BENCHMARK(bench_parse_new_order_single);

void bench_parse_cancel(benchmark::State& state) {
    const auto buf = bytes_of(g_cancel);
    for (auto _ : state) {
        auto r = lob::fix::parse(buf);
        benchmark::DoNotOptimize(r);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(bench_parse_cancel);

}  // namespace
