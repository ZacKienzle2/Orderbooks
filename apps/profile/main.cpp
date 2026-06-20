// lob_profile
// -----------
// Synthetic-data engine profiler. Drives one engine on one thread with a chosen
// deterministic workload, so an external analyser (perf stat, perf record, a
// sanitizer build) reads counters attributable to the engine rather than to a
// random op-dispatch or to thread scheduling. The flow is generated, so no
// market data is needed.
//
// Each workload prints reference cycles per operation over its timed region.
// The dispatch within a workload is deterministic, so its branch profile
// belongs to the engine, not to the driver picking the next op. Pre-population
// runs outside the timed region. Run the binary under perf stat or a sanitizer
// build to attach counters or fault detection; scripts/profile.sh drives that.
//
// Workloads:
//   deep      depth-maintaining mix (replace, price-move modify, qty modify) on
//             a deep two-sided book; the realistic resting-path profile.
//   submit    resting submits with a paired cancel to bound the book.
//   cancel    cancels of a pre-built book.
//   modifyp   non-crossing price-move modifies on a one-sided book.
//   modifyq   quantity-only modifies in place.
//   cross     marketable orders sweeping a replenished opposite side.
//   sweep     one aggressor draining a tall single-price FIFO per iteration.

#include <lob/config.hpp>
#include <lob/engine.hpp>
#include <lob/messages.hpp>
#include <lob/types.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

struct null_pub {
    void publish(const lob::fill_msg&) noexcept {}

    void publish(const lob::top_msg&) noexcept {}

    void publish(const lob::trade_msg&) noexcept {}

    void publish(const lob::self_trade_msg&) noexcept {}
};

constexpr std::size_t ticks = 4096;
constexpr std::size_t max_orders = std::size_t{1} << 17;
constexpr lob::tick_t mid = ticks / 2;
using eng_t = lob::engine<null_pub, ticks, max_orders>;

[[nodiscard]] std::uint64_t now_tsc() noexcept {
#if defined(__x86_64__) || defined(__i386__)
    return __builtin_ia32_rdtsc();
#else
    return 0;
#endif
}

std::uint64_t splitmix(std::uint64_t& s) noexcept {
    std::uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

struct result {
    double cyc_per_op{0.0};
    std::uint64_t fills{0};
};

struct rec {
    lob::order_id_t id;
    lob::tick_t px;
    bool bid;
};

// Non-crossing resting order. Bids sit below mid, asks above, so a fresh book
// builds depth without matching on submit.
lob::submit_msg rest(std::uint64_t& rng, lob::order_id_t id) noexcept {
    const auto r = splitmix(rng);
    const bool bid = (r & 1U) != 0;
    const auto off = static_cast<lob::tick_t>(1 + splitmix(rng) % (mid - 2));
    return {.id = id,
            .px = bid ? (mid - off) : (mid + off),
            .qty = 1 + splitmix(rng) % 100,
            .s = bid ? lob::side::bid : lob::side::ask,
            .t = lob::tif::gtc,
            ._pad = 0,
            .account_id = static_cast<lob::account_id_t>(splitmix(rng) % 6)};
}

result run_deep(eng_t& eng, std::uint64_t ops, std::size_t depth, std::uint64_t seed) {
    if (depth == 0) {
        return {};
    }
    std::uint64_t rng = seed;
    std::vector<rec> live;
    live.reserve(depth + 16);
    lob::order_id_t next = 1;
    for (std::size_t i = 0; i < depth; ++i) {
        const auto m = rest(rng, next++);
        eng.on_submit(m);
        live.push_back({m.id, m.px, m.s == lob::side::bid});
    }
    const auto t0 = now_tsc();
    for (std::uint64_t i = 0; i < ops; ++i) {
        const auto sel = i % 20;  // 10 replace, 6 modify-px, 4 modify-qty
        const std::size_t k = splitmix(rng) % live.size();
        if (sel < 10) {
            eng.on_cancel(lob::cancel_msg{.id = live[k].id});
            const auto m = rest(rng, next++);
            eng.on_submit(m);
            live[k] = {m.id, m.px, m.s == lob::side::bid};
        } else if (sel < 16) {
            const bool bid = live[k].bid;
            const auto off = static_cast<lob::tick_t>(1 + splitmix(rng) % (mid - 2));
            const auto px = bid ? (mid - off) : (mid + off);
            eng.on_modify(lob::modify_msg{
                .id = live[k].id, .new_px = px, .new_qty = 1 + splitmix(rng) % 100});
            live[k].px = px;
        } else {
            eng.on_modify(lob::modify_msg{
                .id = live[k].id, .new_px = live[k].px, .new_qty = 1 + splitmix(rng) % 100});
        }
    }
    return {static_cast<double>(now_tsc() - t0) / static_cast<double>(ops), 0};
}

result run_submit(eng_t& eng, std::uint64_t ops, std::size_t depth, std::uint64_t seed) {
    if (depth == 0) {
        return {};
    }
    std::uint64_t rng = seed;
    lob::order_id_t next = 1;
    for (std::size_t i = 0; i < depth; ++i) {
        eng.on_submit(rest(rng, next++));
    }
    lob::order_id_t id = 1;
    const auto t0 = now_tsc();
    for (std::uint64_t i = 0; i < ops; ++i) {
        eng.on_cancel(lob::cancel_msg{.id = id});
        eng.on_submit(rest(rng, id));
        id = id % depth + 1;
    }
    return {static_cast<double>(now_tsc() - t0) / static_cast<double>(ops), 0};
}

result run_cancel(eng_t& eng, std::uint64_t ops, std::size_t /*depth*/, std::uint64_t seed) {
    std::uint64_t rng = seed;
    std::vector<lob::order_id_t> ids;
    ids.reserve(ops);
    lob::order_id_t next = 1;
    for (std::uint64_t i = 0; i < ops; ++i) {
        const auto m = rest(rng, next++);
        eng.on_submit(m);
        ids.push_back(m.id);
    }
    const auto t0 = now_tsc();
    for (const auto id : ids) {
        eng.on_cancel(lob::cancel_msg{.id = id});
    }
    return {static_cast<double>(now_tsc() - t0) / static_cast<double>(ops), 0};
}

result run_modifyp(eng_t& eng, std::uint64_t ops, std::size_t depth, std::uint64_t seed) {
    if (depth == 0) {
        return {};
    }
    std::uint64_t rng = seed;
    constexpr lob::tick_t lo = mid + 1;
    constexpr lob::tick_t hi = ticks - 1;
    for (lob::order_id_t i = 1; i <= depth; ++i) {
        eng.on_submit({.id = i,
                       .px = static_cast<lob::tick_t>(lo + splitmix(rng) % (hi - lo)),
                       .qty = 1 + splitmix(rng) % 50,
                       .s = lob::side::ask,
                       .t = lob::tif::gtc,
                       ._pad = 0,
                       .account_id = 0});
    }
    const auto t0 = now_tsc();
    for (std::uint64_t i = 0; i < ops; ++i) {
        const lob::order_id_t id = 1 + splitmix(rng) % depth;
        eng.on_modify(
            lob::modify_msg{.id = id,
                            .new_px = static_cast<lob::tick_t>(lo + splitmix(rng) % (hi - lo)),
                            .new_qty = 1 + splitmix(rng) % 50});
    }
    return {static_cast<double>(now_tsc() - t0) / static_cast<double>(ops), 0};
}

result run_modifyq(eng_t& eng, std::uint64_t ops, std::size_t depth, std::uint64_t seed) {
    if (depth == 0) {
        return {};
    }
    std::uint64_t rng = seed;
    std::vector<rec> live;
    live.reserve(depth);
    lob::order_id_t next = 1;
    for (std::size_t i = 0; i < depth; ++i) {
        const auto m = rest(rng, next++);
        eng.on_submit(m);
        live.push_back({m.id, m.px, m.s == lob::side::bid});
    }
    const auto t0 = now_tsc();
    for (std::uint64_t i = 0; i < ops; ++i) {
        const auto& e = live[splitmix(rng) % live.size()];
        eng.on_modify(
            lob::modify_msg{.id = e.id, .new_px = e.px, .new_qty = 1 + splitmix(rng) % 100});
    }
    return {static_cast<double>(now_tsc() - t0) / static_cast<double>(ops), 0};
}

result run_cross(eng_t& eng, std::uint64_t ops, std::size_t depth, std::uint64_t seed) {
    std::uint64_t rng = seed;
    lob::order_id_t next = 1;
    for (std::size_t i = 0; i < depth; ++i) {
        eng.on_submit(rest(rng, next++));
    }
    lob::order_id_t taker = 1'000'000'000;
    const auto t0 = now_tsc();
    for (std::uint64_t i = 0; i < ops; ++i) {
        const bool bid = (i & 1U) != 0;
        eng.on_submit({.id = taker++,
                       .px = static_cast<lob::tick_t>(bid ? ticks - 2 : 1),
                       .qty = 2,
                       .s = bid ? lob::side::bid : lob::side::ask,
                       .t = lob::tif::ioc,
                       ._pad = 0,
                       .account_id = 0});
        if ((i & 1023U) == 0) {
            for (int k = 0; k < 32; ++k) {
                eng.on_submit(rest(rng, next++));
            }
        }
    }
    return {static_cast<double>(now_tsc() - t0) / static_cast<double>(ops), 0};
}

result run_sweep(eng_t& eng, std::uint64_t ops, std::size_t depth, std::uint64_t /*seed*/) {
    constexpr lob::tick_t px = mid;
    const lob::qty_t qd = depth;
    lob::order_id_t maker = 1;
    lob::order_id_t taker = 1'000'000'000;
    const auto refill = [&] {
        for (std::size_t i = 0; i < depth; ++i) {
            eng.on_submit({.id = maker++,
                           .px = px,
                           .qty = 1,
                           .s = lob::side::ask,
                           .t = lob::tif::gtc,
                           ._pad = 0,
                           .account_id = 0});
        }
    };
    refill();
    // ops counts total fills, so one aggressor consumes depth of them. Bounds
    // the work the way the other workloads bound it by their op count.
    const std::uint64_t iters = depth > 0 ? ops / depth : 0;
    std::uint64_t cyc = 0;
    for (std::uint64_t i = 0; i < iters; ++i) {
        const auto t0 = now_tsc();
        eng.on_submit({.id = taker++,
                       .px = px,
                       .qty = qd,
                       .s = lob::side::bid,
                       .t = lob::tif::ioc,
                       ._pad = 0,
                       .account_id = 0});
        cyc += now_tsc() - t0;
        refill();
    }
    const auto fills = iters * depth;
    return {fills > 0 ? static_cast<double>(cyc) / static_cast<double>(fills) : 0.0, 0};
}

struct workload {
    const char* name;
    result (*fn)(eng_t&, std::uint64_t, std::size_t, std::uint64_t);
    const char* note;
};

constexpr workload workloads[] = {
    {"deep", run_deep, "depth-maintaining replace + modify mix"},
    {"submit", run_submit, "resting submit (paired cancel)"},
    {"cancel", run_cancel, "cancel of a pre-built book"},
    {"modifyp", run_modifyp, "non-crossing price-move modify"},
    {"modifyq", run_modifyq, "quantity-only modify"},
    {"cross", run_cross, "marketable cross with replenish"},
    {"sweep", run_sweep, "deep single-price FIFO sweep (cyc per fill)"},
};

struct args {
    std::string workload{"deep"};
    std::uint64_t ops{20'000'000};
    std::size_t depth{40'000};
    std::uint64_t seed{0xC0FFEEULL};
    bool list{false};
};

args parse_args(int argc, char** argv) {
    args a;
    int i = 1;
    while (i < argc) {
        const std::string s = argv[i];
        const bool has_val = i + 1 < argc;
        if (s == "--workload" && has_val) {
            a.workload = argv[i + 1];
            i += 2;
        } else if (s == "--ops" && has_val) {
            a.ops = std::strtoull(argv[i + 1], nullptr, 10);
            i += 2;
        } else if (s == "--depth" && has_val) {
            a.depth = static_cast<std::size_t>(std::strtoull(argv[i + 1], nullptr, 10));
            i += 2;
        } else if (s == "--seed" && has_val) {
            a.seed = std::strtoull(argv[i + 1], nullptr, 10);
            i += 2;
        } else if (s == "--list") {
            a.list = true;
            ++i;
        } else {
            ++i;
        }
    }
    return a;
}

}  // namespace

int main(int argc, char** argv) {
    const args a = parse_args(argc, argv);
    if (a.list) {
        for (const auto& w : workloads) {
            std::printf("%-9s %s\n", w.name, w.note);
        }
        return 0;
    }
    const workload* chosen = nullptr;
    for (const auto& w : workloads) {
        if (a.workload == w.name) {
            chosen = &w;
            break;
        }
    }
    if (chosen == nullptr) {
        std::fprintf(stderr, "unknown workload %s; --list to see options\n", a.workload.c_str());
        return 2;
    }
    null_pub pub;
    // The engine embeds a multi-megabyte arena, so it lives on the heap.
    const auto eng = std::make_unique<eng_t>(pub, lob::engine_config{});
    const result r = chosen->fn(*eng, a.ops, a.depth, a.seed);
    std::printf("workload=%-8s ops=%llu depth=%zu  %.1f cyc/op\n",
                chosen->name,
                static_cast<unsigned long long>(a.ops),
                a.depth,
                r.cyc_per_op);
    return 0;
}
