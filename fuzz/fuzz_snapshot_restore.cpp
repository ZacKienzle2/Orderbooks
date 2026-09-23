// Coverage-guided fuzzing of snapshot restore.
//
// Restore is the second trust boundary: the bytes come from a file or a peer
// rather than from this process, and they decide how many records follow,
// what price each order sits at, and which enumerator each byte means. A
// record the engine could never have written must be rejected before it
// reaches an enum cast or an unchecked ladder index, and a rejection must
// leave the engine cleared rather than half-populated.
//
// The harness asserts the two things that must hold however the bytes were
// produced. If restore accepts, the book it built is internally consistent,
// which is the same invariant the engine's own property checks after every
// command. If restore rejects, the engine is empty. The sanitizers catch the
// rest: a length field that walks off the buffer, an unaligned read, an
// uninitialised byte.

#include <lob/engine.hpp>
#include <lob/snapshot.hpp>
#include <lob/types.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace {

constexpr std::size_t word_bits = std::numeric_limits<std::uint64_t>::digits;
constexpr std::size_t ticks = word_bits * word_bits;
constexpr std::size_t max_orders = 1024;

// Counts nothing and keeps nothing: restore publishes no events, and a
// fuzzing run should not pay for recording them.
struct null_publisher {
    void publish(const lob::fill_msg&) noexcept {}
    void publish(const lob::top_msg&) noexcept {}
    void publish(const lob::trade_msg&) noexcept {}
    void publish(const lob::self_trade_msg&) noexcept {}
    void publish(const lob::reject_msg&) noexcept {}
};

using engine_t = lob::engine<null_publisher, ticks, max_orders>;

// Every level's cached aggregate equals the sum of its FIFO, and a level the
// bitmap reports as best actually holds quantity.
template <class Book>
void assert_side_consistent(const Book& book) {
    lob::qty_t total = 0;
    for (lob::tick_t px = 0; px < ticks; ++px) {
        lob::qty_t sum = 0;
        for (const auto& o : book.level_at(px).fifo) {
            assert(o.remaining > 0);
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
    null_publisher pub;
    engine_t eng{pub, lob::engine_config{}};

    lob::vector_snapshot_buffer buf;
    buf.write(std::span<const std::byte>{reinterpret_cast<const std::byte*>(data), size});
    buf.rewind();

    if (!eng.restore(buf)) {
        // A rejected snapshot leaves nothing behind.
        assert(!eng.book_view().bids().best().has_value());
        assert(!eng.book_view().asks().best().has_value());
        return 0;
    }

    assert_side_consistent(eng.book_view().bids());
    assert_side_consistent(eng.book_view().asks());

    // Whatever restore accepted, the engine must be able to write out again
    // and read back: a warm start is only useful if it is repeatable.
    lob::vector_snapshot_buffer again;
    eng.snapshot(again);
    again.rewind();
    engine_t second{pub, lob::engine_config{}};
    assert(second.restore(again));
    for (lob::tick_t px = 0; px < ticks; ++px) {
        assert(eng.book_view().bids().aggregate_at(px) ==
               second.book_view().bids().aggregate_at(px));
        assert(eng.book_view().asks().aggregate_at(px) ==
               second.book_view().asks().aggregate_at(px));
    }
    return 0;
}
