// Tests of transport internals that cannot be provoked reliably through real sockets.

#include "receive_outcome.hpp"

#include <boost/asio/error.hpp>
#include <boost/throw_exception.hpp>
#include <gtest/gtest.h>
#include <stdexcept>

namespace {

using cigi::ErrorCode;
using cigi::transport::detail::classify_receive;
using cigi::transport::detail::ReceiveAction;
namespace err = boost::asio::error;

TEST(ReceiveOutcome, SuccessDelivers) {
    EXPECT_EQ(classify_receive({}, 16, 16).action, ReceiveAction::Deliver);
    EXPECT_EQ(classify_receive({}, 0, 0).action, ReceiveAction::Deliver);
}

TEST(ReceiveOutcome, SpareByteUsedMeansTooBig) {
    const auto o = classify_receive({}, 17, 16);
    EXPECT_EQ(o.action, ReceiveAction::Fail);
    EXPECT_EQ(o.error.code, ErrorCode::BufferTooSmall);
}

TEST(ReceiveOutcome, MessageSizeMeansTooBig) {
    const auto o = classify_receive(err::make_error_code(err::message_size), 0, 16);
    EXPECT_EQ(o.action, ReceiveAction::Fail);
    EXPECT_EQ(o.error.code, ErrorCode::BufferTooSmall);
}

TEST(ReceiveOutcome, IcmpNotificationsAreRetried) {
    EXPECT_EQ(classify_receive(err::make_error_code(err::connection_reset), 0, 16).action,
              ReceiveAction::Retry);
    EXPECT_EQ(classify_receive(err::make_error_code(err::connection_refused), 0, 16).action,
              ReceiveAction::Retry);
}

TEST(ReceiveOutcome, OtherErrorsKeepOsNumber) {
    const auto ec = err::make_error_code(err::network_down);
    const auto o = classify_receive(ec, 0, 16);
    EXPECT_EQ(o.action, ReceiveAction::Fail);
    EXPECT_EQ(o.error.code, ErrorCode::TransportError);
    EXPECT_EQ(o.error.os_error, ec.value());
}

#ifdef BOOST_NO_EXCEPTIONS
// This translation unit is built with -fno-exceptions (see tests/CMakeLists.txt), so Boost routes
// throw_exception to the transport's definition, which aborts.
TEST(NoExceptions, BoostThrowExceptionAborts) {
    EXPECT_DEATH(boost::throw_exception(std::runtime_error("boom")), "");
    EXPECT_DEATH(boost::throw_exception(std::runtime_error("boom"), BOOST_CURRENT_LOCATION), "");
}
#endif

} // namespace
