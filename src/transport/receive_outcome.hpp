#pragma once

// Internal (not installed): maps the outcome of one Asio receive onto the transport's result.
// Kept separate from udp.cpp so every branch can be unit-tested without provoking OS failures.

#include "cigi/error.hpp"

#include <boost/asio/error.hpp>
#include <boost/system/error_code.hpp>
#include <cstddef>

namespace cigi::transport::detail {

/// OS error number for `Error::os_error`: only errors from the system category carry one
/// (errno on POSIX, the WSA code on Windows); Asio's own categories (netdb, misc...) give 0.
inline int os_error_of(const boost::system::error_code& ec) {
    return ec.category() == boost::system::system_category() ? ec.value() : 0;
}

enum class ReceiveAction : unsigned char {
    Deliver, ///< A datagram that fits was received.
    Retry,   ///< Spurious notification (see below): receive again.
    Fail,    ///< Report `ReceiveOutcome::error`.
};

struct ReceiveOutcome {
    ReceiveAction action = ReceiveAction::Deliver;
    Error error{.code = ErrorCode::TransportError};
};

/// `received` counts bytes written into [caller buffer + one spare byte]; a datagram larger than
/// `capacity` is detected either by the spare byte being used or by the OS reporting message_size.
inline ReceiveOutcome classify_receive(const boost::system::error_code& ec, std::size_t received,
                                       std::size_t capacity) {
    namespace err = boost::asio::error;
    // PLATFORM: after a send to a closed port, Windows reports ICMP "port unreachable" on the next
    // receive as connection_reset (WSAECONNRESET); some stacks report connection_refused. Neither
    // is a real failure for a connectionless transport, so they are skipped.
    if (ec == err::connection_reset || ec == err::connection_refused) {
        return {.action = ReceiveAction::Retry, .error = {.code = ErrorCode::TransportError}};
    }
    if (ec == err::message_size || (!ec && received > capacity)) {
        return {.action = ReceiveAction::Fail, .error = {.code = ErrorCode::BufferTooSmall}};
    }
    if (ec) {
        return {.action = ReceiveAction::Fail,
                .error = {.code = ErrorCode::TransportError, .os_error = os_error_of(ec)}};
    }
    return {};
}

} // namespace cigi::transport::detail
