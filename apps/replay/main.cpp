// lob_replay
// ----------
// Drive the matching engine with a deterministic random command stream and
// emit every event as one JSON Lines record per line. Output is consumed by
// the Python analysis and visualisation harness under scripts/.

#include <lob/engine.hpp>
#include <lob/json_recorder.hpp>
#include <lob/messages.hpp>
#include <lob/types.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr std::size_t default_ticks = 256;
constexpr std::size_t default_max_orders = 1024;

struct args {
    std::uint64_t seed{0xC0FFEE};
    std::size_t commands{10'000};
    std::string out_path;
    std::uint32_t accounts{4};
    int self_cross{0};
};

[[noreturn]] void usage(int code) {
    std::cerr
        << "usage: lob_replay [options]\n"
           "  --seed N         PRNG seed (default 0xC0FFEE)\n"
           "  --commands N     number of commands to issue (default 10000)\n"
           "  --output PATH    write JSON Lines to PATH (default stdout)\n"
           "  --accounts N     number of distinct account ids 1..N (default 4)\n"
           "  --self-cross K   0 cancel_newest, 1 cancel_oldest, 2 decrement_trade (default 0)\n"
           "  --help           show this help\n";
    std::exit(code);
}

args parse_args(int argc, char** argv) {
    args a;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        auto next = [&](const char* k) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << k << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (s == "--seed")
            a.seed = std::stoull(next("--seed"), nullptr, 0);
        else if (s == "--commands")
            a.commands = std::stoul(next("--commands"));
        else if (s == "--output")
            a.out_path = next("--output");
        else if (s == "--accounts")
            a.accounts = static_cast<std::uint32_t>(std::stoul(next("--accounts")));
        else if (s == "--self-cross")
            a.self_cross = std::stoi(next("--self-cross"));
        else if (s == "--help")
            usage(0);
        else {
            std::cerr << "unknown argument: " << s << "\n";
            usage(2);
        }
    }
    return a;
}

}  // namespace

int main(int argc, char** argv) {
    const auto a = parse_args(argc, argv);

    std::ofstream file_sink;
    std::ostream* out = &std::cout;
    if (!a.out_path.empty()) {
        file_sink.open(a.out_path);
        if (!file_sink) {
            std::cerr << "cannot open " << a.out_path << " for write\n";
            return 1;
        }
        out = &file_sink;
    }

    using publisher_t = lob::json_recorder;
    using engine_t = lob::engine<publisher_t, default_ticks, default_max_orders>;

    publisher_t pub{*out};
    const auto policy = static_cast<lob::self_cross_policy>(a.self_cross);
    engine_t eng{pub, lob::engine_config{.self_cross = policy}};

    std::mt19937_64 rng{a.seed};
    std::uniform_int_distribution<lob::tick_t> px{0, default_ticks - 1};
    std::uniform_int_distribution<lob::qty_t> qty{1, 50};
    std::uniform_int_distribution<int> side_dist{0, 1};
    std::uniform_int_distribution<int> tif_dist{0, 2};
    std::uniform_int_distribution<int> op_dist{0, 9};
    std::uniform_int_distribution<std::uint32_t> acct_dist{1, a.accounts};

    std::vector<lob::order_id_t> live;
    live.reserve(a.commands);
    lob::order_id_t next_id = 1;

    for (std::size_t step = 0; step < a.commands; ++step) {
        const auto roll = op_dist(rng);
        if (!live.empty() && roll < 2) {
            std::uniform_int_distribution<std::size_t> pick{0, live.size() - 1};
            const auto k = pick(rng);
            eng.on_cancel({.id = live[k]});
            live[k] = live.back();
            live.pop_back();
        } else if (!live.empty() && roll < 3) {
            std::uniform_int_distribution<std::size_t> pick{0, live.size() - 1};
            const auto k = pick(rng);
            eng.on_modify({.id = live[k], .new_px = px(rng), .new_qty = qty(rng)});
        } else {
            const auto id = next_id++;
            live.push_back(id);
            eng.on_submit({
                .id = id,
                .px = px(rng),
                .qty = qty(rng),
                .s = (side_dist(rng) == 0) ? lob::side::bid : lob::side::ask,
                .t = static_cast<lob::tif>(tif_dist(rng)),
                ._pad = 0,
                .account_id = acct_dist(rng),
            });
        }
    }

    out->flush();
    return 0;
}
