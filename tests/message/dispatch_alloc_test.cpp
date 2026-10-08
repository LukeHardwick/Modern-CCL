// Dispatching must not allocate. This binary replaces the global operator new to count calls, so
// it is kept apart from the other tests.

#include "../support/bytes.hpp"
#include "cigi/dispatch.hpp"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <gtest/gtest.h>
#include <new>

namespace {
std::atomic<std::size_t> allocations{
    0}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
} // namespace

// NOLINTBEGIN(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,misc-use-internal-linkage,readability-inconsistent-declaration-parameter-name)
void* counted_alloc(std::size_t size, std::size_t align) noexcept {
    allocations.fetch_add(1, std::memory_order_relaxed);
    void* p = nullptr;
    if (align <= alignof(std::max_align_t)) {
        p = std::malloc(size);
    } else if (posix_memalign(&p, align, size) != 0) {
        p = nullptr;
    }
    return p;
}
void* checked(void* p) {
    if (p == nullptr) {
        std::abort(); // the library does not use exceptions
    }
    return p;
}
void* operator new(std::size_t size) {
    return checked(counted_alloc(size, 1));
}
void* operator new[](std::size_t size) {
    return checked(counted_alloc(size, 1));
}
void* operator new(std::size_t size, std::align_val_t al) {
    return checked(counted_alloc(size, static_cast<std::size_t>(al)));
}
void* operator new[](std::size_t size, std::align_val_t al) {
    return checked(counted_alloc(size, static_cast<std::size_t>(al)));
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    return counted_alloc(size, 1);
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return counted_alloc(size, 1);
}
void* operator new(std::size_t size, std::align_val_t al, const std::nothrow_t&) noexcept {
    return counted_alloc(size, static_cast<std::size_t>(al));
}
void* operator new[](std::size_t size, std::align_val_t al, const std::nothrow_t&) noexcept {
    return counted_alloc(size, static_cast<std::size_t>(al));
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete[](void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}
void operator delete(void* p, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept {
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

TEST(DispatchAllocations, CounterSeesEveryNewForm) {
    const std::size_t before = allocations.load();
    // NOLINTBEGIN(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)
    delete new int(1);
    delete[] new int[2];
    struct alignas(64) Aligned {
        char c;
    };
    delete new Aligned;
    delete new (std::nothrow) int(2);
    // NOLINTEND(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)
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
