#ifndef LOB_PUBLISHER_SINK_HPP
#define LOB_PUBLISHER_SINK_HPP

#include <lob/concepts.hpp>
#include <lob/messages.hpp>

#include <cstdint>

namespace lob {

// Bridges the merger's event stream onto a publisher.
//
// egress_merger forwards each merged event through a merge_sink, while the
// engine-facing sinks (json_recorder, a ring egress, any analysis hook)
// satisfy the publisher concept with typed publish overloads. publisher_sink
// adapts one to the other by decoding the event tagged union and calling the
// matching publish overload, so any existing publisher becomes a downstream
// of the fanned-in stream without a bespoke adapter.
//
// This is the seam between the parallel matching core and a single streaming
// consumer. The merge sequence is carried for sinks that want a global order
// stamp; a publisher that orders by arrival ignores it, since the merger
// already forwards events in a single, totally ordered sequence.
template <publisher P>
class publisher_sink {
  public:
    explicit publisher_sink(P& pub) noexcept : pub_(&pub) {}

    void on_event(const event& e, std::uint64_t /*merge_seq*/) noexcept {
        switch (e.k) {
        case event::kind::fill:
            pub_->publish(e.body.fill);
            break;
        case event::kind::top:
            pub_->publish(e.body.top);
            break;
        case event::kind::trade:
            pub_->publish(e.body.trade);
            break;
        case event::kind::self_trade:
            pub_->publish(e.body.self_trade);
            break;
        }
    }

    [[nodiscard]] P& publisher() noexcept { return *pub_; }

    [[nodiscard]] const P& publisher() const noexcept { return *pub_; }

  private:
    P* pub_;
};

}  // namespace lob

#endif  // LOB_PUBLISHER_SINK_HPP
