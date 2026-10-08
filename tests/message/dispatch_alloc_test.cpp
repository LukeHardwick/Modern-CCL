// Dispatching must not allocate. This binary replaces the global operator new to count calls, so
// it is kept apart from the other tests.

#include "../support/bytes.hpp"
#include "cigi/dispatch.hpp"

#include <atomic>
#include <cstdlib>
#include <gtest/gtest.h>
#include <new>

namespace {
std::atomic<std::size_t> allocations{
    0}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
} // namespace

// NOLINTBEGIN(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,misc-use-internal-linkage,readability-inconsistent-declaration-parameter-name)
void* operator new(std::size_t size) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(size)) {
        return p;
    }
    std::abort(); // the library does not use exceptions
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t /*size*/) noexcept {
    std::free(p);
}
// NOLINTEND(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,misc-use-internal-linkage,readability-inconsistent-declaration-parameter-name)

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
