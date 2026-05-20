#include <lob/json_recorder.hpp>
#include <lob/messages.hpp>

#include <algorithm>
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("json_recorder encodes fill_msg as a single line", "[json_recorder]") {
    std::ostringstream out;
    lob::json_recorder rec{out};
    rec.publish(lob::fill_msg{.maker = 7, .taker = 9, .px = 100, .qty = 4, .seq = 42});
    REQUIRE(out.str() == R"({"kind":"fill","seq":42,"maker":7,"taker":9,"px":100,"qty":4})"
                         "\n");
}

TEST_CASE("json_recorder encodes top_msg with the expected field order", "[json_recorder]") {
    std::ostringstream out;
    lob::json_recorder rec{out};
    rec.publish(lob::top_msg{
        .bid_px = 99,
        .ask_px = 101,
        .bid_qty = 5,
        .ask_qty = 6,
        .seq = 17,
    });
    REQUIRE(out.str() ==
            R"({"kind":"top","seq":17,"bid_px":99,"ask_px":101,"bid_qty":5,"ask_qty":6})"
            "\n");
}

TEST_CASE("json_recorder encodes trade_msg", "[json_recorder]") {
    std::ostringstream out;
    lob::json_recorder rec{out};
    rec.publish(lob::trade_msg{.px = 250, .qty = 12, .seq = 3});
    REQUIRE(out.str() == R"({"kind":"trade","seq":3,"px":250,"qty":12})"
                         "\n");
}

TEST_CASE("json_recorder encodes self_trade_msg", "[json_recorder]") {
    std::ostringstream out;
    lob::json_recorder rec{out};
    rec.publish(lob::self_trade_msg{
        .aggressor = 11,
        .resting = 13,
        .account = 7,
        .px = 99,
        .qty = 2,
        .seq = 99,
    });
    REQUIRE(
        out.str() ==
        R"({"kind":"self_trade","seq":99,"aggressor":11,"resting":13,"account":7,"px":99,"qty":2})"
        "\n");
}

TEST_CASE("json_recorder appends multiple events without delimiter loss", "[json_recorder]") {
    std::ostringstream out;
    lob::json_recorder rec{out};
    rec.publish(lob::top_msg{.bid_px = 1, .ask_px = 2, .bid_qty = 3, .ask_qty = 4, .seq = 1});
    rec.publish(lob::trade_msg{.px = 2, .qty = 1, .seq = 2});

    const std::string s = out.str();
    REQUIRE(s.find('\n') != std::string::npos);
    REQUIRE(s.rfind('\n') == s.size() - 1);
    REQUIRE(std::count(s.begin(), s.end(), '\n') == 2);
}
