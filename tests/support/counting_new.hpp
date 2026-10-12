#pragma once

// Replaces the global operator new/delete with versions that count allocations, for the
// "must not allocate" tests. Include it from exactly ONE translation unit of a test binary, and
// keep that binary separate from the others (clang-tidy is off for these targets).
//
// `allocations` counts every operator new call, in all of its forms.

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>

#ifdef _WIN32
#include <malloc.h>
#endif

inline std::atomic<std::size_t> allocations{0};

inline void* counted_alloc(std::size_t size, std::size_t align) noexcept {
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (align <= alignof(std::max_align_t)) {
        return std::malloc(size);
    }
#ifdef _WIN32
    // PLATFORM: MSVC has no posix_memalign; memory from _aligned_malloc must go to _aligned_free.
    return _aligned_malloc(size, align);
#else
    void* p = nullptr;
    return posix_memalign(&p, align, size) == 0 ? p : nullptr;
#endif
}
inline void counted_free(void* p, bool aligned) noexcept {
#ifdef _WIN32
    if (aligned) {
        _aligned_free(p); // PLATFORM: pairs with _aligned_malloc above
        return;
    }
#else
    static_cast<void>(aligned);
#endif
    std::free(p);
}
inline void* checked_alloc(void* p) {
    if (p == nullptr) {
        std::abort(); // the library does not use exceptions
    }
    return p;
}

void* operator new(std::size_t size) {
    return checked_alloc(counted_alloc(size, 1));
}
void* operator new[](std::size_t size) {
    return checked_alloc(counted_alloc(size, 1));
}
void* operator new(std::size_t size, std::align_val_t al) {
    return checked_alloc(counted_alloc(size, static_cast<std::size_t>(al)));
}
void* operator new[](std::size_t size, std::align_val_t al) {
    return checked_alloc(counted_alloc(size, static_cast<std::size_t>(al)));
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
    counted_free(p, false);
}
void operator delete[](void* p) noexcept {
    counted_free(p, false);
}
void operator delete(void* p, std::size_t) noexcept {
    counted_free(p, false);
}
void operator delete[](void* p, std::size_t) noexcept {
    counted_free(p, false);
}
void operator delete(void* p, std::align_val_t) noexcept {
    counted_free(p, true);
}
void operator delete[](void* p, std::align_val_t) noexcept {
    counted_free(p, true);
}
void operator delete(void* p, std::size_t, std::align_val_t) noexcept {
    counted_free(p, true);
}
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept {
    counted_free(p, true);
}
