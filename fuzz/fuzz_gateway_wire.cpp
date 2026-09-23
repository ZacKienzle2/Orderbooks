// Coverage-guided fuzzing of the gateway's decode, validate and dispatch step.
//
// A client's bytes become wire_order records, each validated and then applied
// to a book. The property is not about any one record: it is that whatever
// sequence arrives, valid or not, the book it leaves behind is a book. Every
// level's aggregate matches its FIFO, no resting order carries zero quantity,
// and the bitmap agrees about where the best price is.
//
// The fuzzer chooses the sequence. Because it sees the coverage, it finds the
// orderings that matter, an order that crosses its own resting side, a modify
// that reprices onto a level it just emptied, a cancel of an id a rejected
// order would have used, without any of them being written down here.

#include <lob/engine.hpp>
#include <lob/types.hpp>

#include "../apps/gateway/wire.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace {

constexpr std::size_t word_bits = std::numeric_limits<std::uint64_t>::digits;
constexpr std::size_t ticks = word_bits * word_bits;
constexpr std::size_t max_orders = 1024;

using lob_gateway::accum_pub;
using lob_gateway::wire_ack;
using lob_gateway::wire_order;

using engine_t = lob::engine<accum_pub, ticks, max_orders>;

template <class Book>
void assert_side_consistent(const Book& book) {
    lob::qty_t total = 0;
    for (lob::tick_t px = 0; px < ticks; ++px) {
        lob::qty_t sum = 0;
        for (const auto& o : book.level_at(px).fifo) {
            assert(o.remaining > 0);
            assert(o.px == px);
            sum += o.remaining;
        }
        assert(book.aggregate_at(px) == sum);
        total += sum;
    }
    const auto best = book.best();
    if (best.has_value())
        assert(book.aggregate_at(*best) > 0);
    else
        assert(total == 0);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    accum_pub pub;
    engine_t eng{pub, lob::engine_config{}};

    for (std::size_t off = 0; off + sizeof(wire_order) <= size; off += sizeof(wire_order)) {
        wire_order wo{};
        std::memcpy(&wo, data + off, sizeof wo);
        wire_ack ack{};
        lob_gateway::apply_order(eng, pub, wo, ack);
        // Every order is answered, and the answer names the order it answers.
        assert(ack.id == wo.id);
    }

    assert_side_consistent(eng.book_view().bids());
    assert_side_consistent(eng.book_view().asks());
    return 0;
}
