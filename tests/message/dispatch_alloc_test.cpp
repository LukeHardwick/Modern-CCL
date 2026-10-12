// Dispatching must not allocate. This binary replaces the global operator new to count calls, so
// it is kept apart from the other tests.

#include "../support/bytes.hpp"
#include "../support/counting_new.hpp"
#include "cigi/dispatch.hpp"

#include <cstddef>
#include <gtest/gtest.h>
#include <new>

namespace {

using namespace cigi;
using namespace cigi::test;

Bytes message() {
    Bytes m(24);
    encode(IgControl{}, m);
    Bytes e(48);
    encode(EntityControl{}, e);
    append(m, e);
    Bytes u(8);
    encode(UserPacket{1}, u);
    append(m, u);
    Bytes bad(8, std::byte{0});
    bad.at(0) = std::byte{2}; // truncated Entity Control: an error, which must not allocate either
    bad.at(1) = std::byte{8};
    append(m, bad);
    return m;
}

TEST(DispatchAllocations, CounterSeesEveryNewForm) {
    const std::size_t before = allocations.load();
    delete new int(1);
    delete[] new int[2];
    struct alignas(64) Aligned {
        char c;
    };
    delete new Aligned;
    delete new (std::nothrow) int(2);
    EXPECT_EQ(allocations.load() - before, 4U);
}

TEST(DispatchAllocations, VisitorAndDispatcherDoNotAllocate) {
    const Bytes m = message();
    const auto view = MessageView::parse(m);
    ASSERT_TRUE(view.has_value());

    int seen = 0;
    int errors = 0;
    Dispatcher<HostToIgPackets> d;
    d.on<IgControl>([&](const IgControl&) { ++seen; });
    d.on<EntityControl>([&](const EntityControl&) { ++seen; });
    d.on_unknown([&](const UnknownPacket&) { ++seen; });
    d.on_error([&](const Error&) { ++errors; });

    const std::size_t before = allocations.load();
    const auto s1 = dispatch<HostToIgPackets>(
        *view, overloaded{[&](const IgControl&) { ++seen; }, [&](const UnknownPacket&) { ++seen; }},
        [&](const Error&) { ++errors; });
    const auto s2 = d.dispatch(*view);
    const std::size_t after = allocations.load();

    EXPECT_EQ(after, before);
    EXPECT_EQ(s1.errors, 1U);
    EXPECT_EQ(s2.handled, 2U);
    EXPECT_EQ(seen, 2 + 3);
    EXPECT_EQ(errors, 2);
}

} // namespace
