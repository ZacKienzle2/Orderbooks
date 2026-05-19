#ifndef LOB_CONCEPTS_HPP
#define LOB_CONCEPTS_HPP

#include <lob/messages.hpp>
#include <lob/types.hpp>

#include <concepts>
#include <cstddef>
#include <span>

namespace lob {

// publisher: sinks for engine-emitted events. The publisher is fed in the
// hot path; every method must be noexcept and ideally inlines to a single
// SPSC ring push.
template <class P>
concept publisher = requires(
    P p, const fill_msg& f, const top_msg& t, const trade_msg& tr, const self_trade_msg& st) {
    { p.publish(f) } noexcept -> std::same_as<void>;
    { p.publish(t) } noexcept -> std::same_as<void>;
    { p.publish(tr) } noexcept -> std::same_as<void>;
    { p.publish(st) } noexcept -> std::same_as<void>;
};

// clock_source: monotonic sequence-stamp provider. Engine assigns seq_t to
// every event from this source.
template <class C>
concept clock_source = requires(C c) {
    { c.now() } noexcept -> std::same_as<seq_t>;
};

// snapshot_sink: byte-oriented sink for serialised engine state.
template <class S>
concept snapshot_sink = requires(S s, std::span<const std::byte> buf) {
    { s.write(buf) } noexcept -> std::same_as<void>;
};

}  // namespace lob

#endif  // LOB_CONCEPTS_HPP
