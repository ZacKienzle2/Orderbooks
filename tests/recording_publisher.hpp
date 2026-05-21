#ifndef LOB_TESTS_RECORDING_PUBLISHER_HPP
#define LOB_TESTS_RECORDING_PUBLISHER_HPP

#include <lob/messages.hpp>

#include <vector>

namespace lob::test {

// In-memory publisher that satisfies the lob::publisher concept and
// records every event for assertion. Single-threaded; not for production.
struct recording_publisher {
    std::vector<fill_msg> fills;
    std::vector<top_msg> tops;
    std::vector<trade_msg> trades;
    std::vector<self_trade_msg> self_trades;

    void publish(const fill_msg& m) noexcept { fills.push_back(m); }

    void publish(const top_msg& m) noexcept { tops.push_back(m); }

    void publish(const trade_msg& m) noexcept { trades.push_back(m); }

    void publish(const self_trade_msg& m) noexcept { self_trades.push_back(m); }

    void clear() noexcept {
        fills.clear();
        tops.clear();
        trades.clear();
        self_trades.clear();
    }
};

}  // namespace lob::test

#endif  // LOB_TESTS_RECORDING_PUBLISHER_HPP
