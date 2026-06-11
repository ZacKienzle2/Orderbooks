#include <lob/fix_parser.hpp>
#include <lob/messages.hpp>

#include <cstddef>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr char k_soh = '\x01';

// Assemble a complete FIX 4.4 message from its body (everything from the
// MsgType field through the SOH before CheckSum). Prepends BeginString(8)
// and BodyLength(9), appends a correctly computed CheckSum(10).
//
// BodyLength counts the bytes after the BodyLength field's SOH up to and
// including the SOH before CheckSum -- exactly the supplied body, which
// already carries its trailing SOH. CheckSum is the sum of every byte up to
// (not including) the CheckSum field, modulo 256, as three zero-padded
// digits. The recomputation here is a deliberately independent reference for
// the parser's own validation.
std::string make_fix(std::string_view body) {
    std::string head = "8=FIX.4.4";
    head += k_soh;
    head += "9=";
    head += std::to_string(body.size());
    head += k_soh;
    head += body;

    unsigned sum = 0;
    for (const char c : head)
        sum += static_cast<unsigned char>(c);
    sum %= 256U;

    char checksum[8];
    std::snprintf(checksum, sizeof(checksum), "10=%03u%c", sum, k_soh);
    head += checksum;
    return head;
}

std::span<const std::byte> bytes_of(const std::string& s) noexcept {
    return {reinterpret_cast<const std::byte*>(s.data()), s.size()};
}

}  // namespace

TEST_CASE("parses a NewOrderSingle into a submit command", "[fix]") {
    const std::string wire = make_fix("35=D\x01"
                                      "11=1001\x01"
                                      "55=AAPL\x01"
                                      "54=1\x01"
                                      "38=50\x01"
                                      "40=2\x01"
                                      "44=100\x01"
                                      "59=1\x01");

    const auto r = lob::fix::parse(bytes_of(wire));

    REQUIRE(r.err == lob::fix::error::ok);
    REQUIRE(r.consumed == wire.size());
    REQUIRE(r.cmd.k == lob::command::kind::submit);
    CHECK(r.cmd.body.submit.id == 1001);
    CHECK(r.cmd.body.submit.px == 100);
    CHECK(r.cmd.body.submit.qty == 50);
    CHECK(r.cmd.body.submit.s == lob::side::bid);
    CHECK(r.cmd.body.submit.t == lob::tif::gtc);
}

TEST_CASE("parses an OrderCancelRequest into a cancel command", "[fix]") {
    const std::string wire = make_fix("35=F\x01"
                                      "11=1002\x01"
                                      "41=1001\x01"
                                      "55=AAPL\x01"
                                      "54=1\x01");

    const auto r = lob::fix::parse(bytes_of(wire));

    REQUIRE(r.err == lob::fix::error::ok);
    REQUIRE(r.cmd.k == lob::command::kind::cancel);
    CHECK(r.cmd.body.cancel.id == 1001);
}

TEST_CASE("parses an OrderCancelReplaceRequest into a modify command", "[fix]") {
    const std::string wire = make_fix("35=G\x01"
                                      "11=1003\x01"
                                      "41=1001\x01"
                                      "55=AAPL\x01"
                                      "54=1\x01"
                                      "38=70\x01"
                                      "40=2\x01"
                                      "44=99\x01");

    const auto r = lob::fix::parse(bytes_of(wire));

    REQUIRE(r.err == lob::fix::error::ok);
    REQUIRE(r.cmd.k == lob::command::kind::modify);
    CHECK(r.cmd.body.modify.id == 1001);
    CHECK(r.cmd.body.modify.new_px == 99);
    CHECK(r.cmd.body.modify.new_qty == 70);
}

TEST_CASE("maps FIX side and time-in-force codes", "[fix]") {
    SECTION("sell maps to ask, IOC maps to ioc") {
        const std::string wire = make_fix("35=D\x01"
                                          "11=7\x01"
                                          "54=2\x01"
                                          "38=5\x01"
                                          "44=42\x01"
                                          "59=3\x01");
        const auto r = lob::fix::parse(bytes_of(wire));
        REQUIRE(r.err == lob::fix::error::ok);
        CHECK(r.cmd.body.submit.s == lob::side::ask);
        CHECK(r.cmd.body.submit.t == lob::tif::ioc);
    }
    SECTION("FOK time-in-force") {
        const std::string wire = make_fix("35=D\x01"
                                          "11=8\x01"
                                          "54=1\x01"
                                          "38=5\x01"
                                          "44=42\x01"
                                          "59=4\x01");
        const auto r = lob::fix::parse(bytes_of(wire));
        REQUIRE(r.err == lob::fix::error::ok);
        CHECK(r.cmd.body.submit.t == lob::tif::fok);
    }
    SECTION("absent time-in-force defaults to GTC") {
        const std::string wire = make_fix("35=D\x01"
                                          "11=9\x01"
                                          "54=1\x01"
                                          "38=5\x01"
                                          "44=42\x01");
        const auto r = lob::fix::parse(bytes_of(wire));
        REQUIRE(r.err == lob::fix::error::ok);
        CHECK(r.cmd.body.submit.t == lob::tif::gtc);
    }
}

TEST_CASE("parses the Account tag into account_id", "[fix]") {
    const std::string wire = make_fix("35=D\x01"
                                      "1=77\x01"
                                      "11=5\x01"
                                      "54=1\x01"
                                      "38=5\x01"
                                      "44=42\x01");
    const auto r = lob::fix::parse(bytes_of(wire));
    REQUIRE(r.err == lob::fix::error::ok);
    CHECK(r.cmd.body.submit.account_id == 77);
}

TEST_CASE("rejects a corrupted checksum", "[fix]") {
    std::string wire = make_fix("35=D\x01"
                                "11=1\x01"
                                "54=1\x01"
                                "38=5\x01"
                                "44=42\x01");
    // Flip the last checksum digit (the byte before the trailing SOH).
    wire[wire.size() - 2] = wire[wire.size() - 2] == '9' ? '0' : '9';

    const auto r = lob::fix::parse(bytes_of(wire));
    CHECK(r.err == lob::fix::error::bad_checksum);
}

TEST_CASE("rejects a wrong begin string", "[fix]") {
    std::string wire = make_fix("35=D\x01"
                                "11=1\x01"
                                "54=1\x01"
                                "38=5\x01"
                                "44=42\x01");
    wire[7] = '2';  // FIX.4.4 -> FIX.4.2

    const auto r = lob::fix::parse(bytes_of(wire));
    CHECK(r.err == lob::fix::error::bad_begin_string);
}

TEST_CASE("reports incomplete on a truncated buffer", "[fix]") {
    const std::string wire = make_fix("35=D\x01"
                                      "11=1\x01"
                                      "54=1\x01"
                                      "38=5\x01"
                                      "44=42\x01");
    const auto full = bytes_of(wire);

    const auto r = lob::fix::parse(full.first(full.size() - 4));
    CHECK(r.err == lob::fix::error::incomplete);
    CHECK(r.consumed == 0);
}

TEST_CASE("reports unsupported message type", "[fix]") {
    // 35=0 is a Heartbeat -- structurally valid, not an order command.
    const std::string wire = make_fix("35=0\x01"
                                      "112=TEST\x01");
    const auto r = lob::fix::parse(bytes_of(wire));
    CHECK(r.err == lob::fix::error::unsupported_msg_type);
}

TEST_CASE("rejects a NewOrderSingle missing a required field", "[fix]") {
    // No OrderQty(38).
    const std::string wire = make_fix("35=D\x01"
                                      "11=1\x01"
                                      "54=1\x01"
                                      "44=42\x01");
    const auto r = lob::fix::parse(bytes_of(wire));
    CHECK(r.err == lob::fix::error::missing_field);
}

TEST_CASE("rejects a non-numeric quantity", "[fix]") {
    const std::string wire = make_fix("35=D\x01"
                                      "11=1\x01"
                                      "54=1\x01"
                                      "38=x\x01"
                                      "44=42\x01");
    const auto r = lob::fix::parse(bytes_of(wire));
    CHECK(r.err == lob::fix::error::bad_field_value);
}

TEST_CASE("consumes exactly one message from a concatenated stream", "[fix]") {
    const std::string first = make_fix("35=D\x01"
                                       "11=1\x01"
                                       "54=1\x01"
                                       "38=5\x01"
                                       "44=42\x01");
    const std::string second = make_fix("35=F\x01"
                                        "11=2\x01"
                                        "41=1\x01");
    const std::string stream = first + second;

    const auto r = lob::fix::parse(bytes_of(stream));
    REQUIRE(r.err == lob::fix::error::ok);
    CHECK(r.consumed == first.size());
    CHECK(r.cmd.k == lob::command::kind::submit);
}
