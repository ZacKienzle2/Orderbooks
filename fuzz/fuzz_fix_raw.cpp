// Coverage-guided fuzzing of the FIX parser against arbitrary bytes.
//
// The parser is a trust boundary: everything it reads came off a socket. No
// input may make it read past the buffer it was given, loop forever, or
// report a result that contradicts itself. Nothing here states which inputs
// to try. libFuzzer derives them from the coverage the parse reaches, which
// reaches cases no one would have thought to write down, and the sanitizers
// decide what counts as a failure: an out-of-bounds read, an uninitialised
// value, undefined behaviour or a hang.
//
// This target feeds the bytes straight through, so it explores the framing:
// short buffers, bad BeginString, malformed BodyLength, truncated fields.
// fuzz_fix_framed.cpp reframes the input so the parse gets past the checksum
// and explores the fields instead.

#include <lob/fix_parser.hpp>
#include <lob/messages.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::byte> in{reinterpret_cast<const std::byte*>(data), size};
    const auto r = lob::fix::parse(in);

    // A successful parse consumed a frame that lies inside the buffer it was
    // given, and a failed one consumed nothing the caller should act on.
    if (r.err == lob::fix::error::ok) {
        assert(r.consumed > 0);
        assert(r.consumed <= size);
        // The command is one of the three the parser produces, with the
        // identity fields it promises. An id of zero or all ones would alias
        // the sentinels the id index reserves.
        switch (r.cmd.k) {
            case lob::command::kind::submit:
                assert(r.cmd.body.submit.id != 0);
                assert(r.cmd.body.submit.id != ~lob::order_id_t{0});
                assert(r.cmd.body.submit.qty > 0);
                break;
            case lob::command::kind::cancel:
                assert(r.cmd.body.cancel.id != 0);
                assert(r.cmd.body.cancel.id != ~lob::order_id_t{0});
                break;
            case lob::command::kind::modify:
                assert(r.cmd.body.modify.id != 0);
                assert(r.cmd.body.modify.id != ~lob::order_id_t{0});
                break;
        }
    } else {
        assert(r.consumed == 0);
    }
    return 0;
}
