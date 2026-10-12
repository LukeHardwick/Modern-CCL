#pragma once

// HAT/HOT answers for the mock IG (ICD 4.1.24 request, 4.2.2 response, 4.2.3 extended response).
// Pure logic: submit() the requests the Host sent, then once per IG frame collect() the packets
// to send back. Nothing is sent from here.
//
// Behaviour (ICD 4.1.24):
//  - Update Period 0 is a one-shot: exactly one answer, at the next collect().
//  - Update Period n > 0 is periodic: an answer at the next collect() and then every nth call,
//    until the Host re-sends the same HAT/HOT ID with Update Period 0 (that cancels the periodic
//    request and, being a one-shot, is answered once) or, for an entity-relative request, until
//    the entity is destroyed.
//  - A test point outside the database bounds is answered with Valid = 0.
//  - Periodic answers carry the Host Frame Number LSN (4.2.2); one-shots leave it 0, since the
//    Host ignores it there.

#include "mock_ig/scene.hpp"

#include <cigi/packets.hpp>
#include <cstddef>
#include <cstdint>
#include <map>
#include <variant>
#include <vector>

namespace cigi::mock {

class HatHotResponder {
public:
    using Reply = std::variant<HatHotResponse, HatHotExtendedResponse>;

    /// Accept a request (one-shot, new periodic, replacement or cancellation as described above).
    void submit(const HatHotRequest& request);

    /// The packets due this IG frame. Resolves the scene, records a HatHotProbe in it for every
    /// answer, and returns the answers in order: one-shots as received, then periodic requests by
    /// HAT/HOT ID. `ig_frame` is only used to label the probes.
    [[nodiscard]] std::vector<Reply> collect(SceneModel& scene, std::uint64_t ig_frame);

    [[nodiscard]] std::size_t periodic_count() const noexcept { return periodic_.size(); }
    [[nodiscard]] std::size_t pending_count() const noexcept { return pending_.size(); }

    /// Answer one request against `scene` right now (no scheduling). `periodic` selects whether
    /// the LSN is filled in. The probe describes the answer.
    [[nodiscard]] static Reply answer(const SceneModel& scene, const HatHotRequest& request,
                                      bool periodic, HatHotProbe& probe);

private:
    struct Periodic {
        HatHotRequest request;
        std::uint8_t frames_until_due = 0;
    };

    std::vector<HatHotRequest> pending_;
    std::map<std::uint16_t, Periodic> periodic_;
};

} // namespace cigi::mock
