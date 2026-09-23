// Coverage-guided fuzzing of the FIX parser's field handling.
//
// A FIX frame ends in a checksum over every preceding byte, so a fuzzer
// mutating raw bytes almost never produces one that validates, and the parse
// stops at the frame check without ever reaching a field. This target treats
// the fuzzer's bytes as the message body and frames them correctly, computing
// BodyLength and CheckSum the way a sender would. The coverage feedback then
// drives the fuzzer through the tag scan, the value conversions and the
// per-message required-field rules.
//
// This is the structure-aware arrangement Langdale and Lemire's parser work
// assumes of any format with a length or a checksum: fuzz the payload, let
// the harness maintain the envelope, and the fuzzer spends its budget on the
// grammar rather than on rediscovering arithmetic.

#include <lob/fix_parser.hpp>
#include <lob/messages.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>

namespace {

constexpr char soh = '\x01';

// Wrap a body in a valid FIX 4.4 envelope: BeginString, BodyLength over the
// body exactly, and the CheckSum of every byte before it.
std::string frame(std::string_view body) {
    std::string wire = "8=FIX.4.4";
    wire += soh;
    wire += "9=";
    wire += std::to_string(body.size());
    wire += soh;
    wire += body;

    unsigned sum = 0;
    for (const char c : wire)
        sum += static_cast<unsigned char>(c);
    sum &= 0xFFU;

    char tail[8];
    std::snprintf(tail, sizeof tail, "10=%03u%c", sum, soh);
    wire += tail;
    return wire;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    // The parser caps a frame at max_message_size, so a body beyond that is
    // rejected on length alone and tells the fuzzer nothing about fields.
    if (size > lob::fix::max_message_size)
        return 0;

    const std::string wire = frame(std::string_view{reinterpret_cast<const char*>(data), size});
    const auto r = lob::fix::parse(
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(wire.data()), wire.size()});

    // The envelope is valid by construction, so the parse must never report a
    // framing failure. Anything it rejects, it rejects on the body.
    assert(r.err != lob::fix::error::bad_begin_string);
    assert(r.err != lob::fix::error::bad_checksum);
    if (r.err == lob::fix::error::ok) {
        // A framed body is complete, so a successful parse consumes the whole
        // frame and nothing beyond it.
        assert(r.consumed == wire.size());
        if (r.cmd.k == lob::command::kind::submit)
            assert(r.cmd.body.submit.qty > 0);
    }
    return 0;
}
