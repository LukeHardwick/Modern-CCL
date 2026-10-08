#pragma once

// Handling the packets of a received message without writing opcode switches.
//
//   dispatch<Set>(view, visitor [, on_error])   compile-time: a visitor (see `overloaded`)
//   Dispatcher<Set>                             runtime: handlers registered with on<P>()
//
// Both walk the MessageView in order. Unknown opcodes are not errors (ICD 2.11 rule 3). A
// malformed packet header ends the walk (the next packet cannot be located); a packet whose body
// fails to decode, or has the wrong direction, is reported and the walk continues.

#include "cigi/message/view.hpp"
#include "cigi/packet_set.hpp"

#include <concepts>
#include <cstddef>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace cigi {

/// Builds one visitor from several lambdas: `overloaded{[](const IgControl&){}, [](auto&){}}`.
// NOLINTBEGIN(readability-identifier-naming,misc-multiple-inheritance): the standard idiom, named
// like std::visit's examples so it reads the same everywhere.
template <class... Fs> struct overloaded : Fs... {
    using Fs::operator()...;
};
// NOLINTEND(readability-identifier-naming,misc-multiple-inheritance)

/// What one dispatch pass did.
struct DispatchStats {
    std::size_t handled = 0; ///< Known packets given to a handler.
    std::size_t ignored = 0; ///< Known packets with no handler.
    std::size_t unknown = 0; ///< Packets with an opcode outside the set (handled or not).
    std::size_t errors = 0;  ///< Header errors (at most one) plus body errors.

    friend constexpr bool operator==(const DispatchStats&, const DispatchStats&) noexcept = default;
};

namespace detail {

/// The error callback used when the caller gives none.
struct IgnoreErrors {
    constexpr void operator()(const Error& /*error*/) const noexcept {}
};

/// Call `f(alternative)` for the alternative `v` holds (std::visit without the exception path).
template <std::size_t I = 0, class V, class F> constexpr void visit_alternative(const V& v, F&& f) {
    if constexpr (I < std::variant_size_v<V>) {
        if (v.index() == I) {
            f(*std::get_if<I>(&v));
        } else {
            visit_alternative<I + 1>(v, std::forward<F>(f));
        }
    }
}

/// The walk shared by dispatch() and Dispatcher. `on_packet(const P&) -> bool` says whether the
/// packet was handled.
template <class Set, class OnPacket, class OnUnknown, class OnError>
constexpr DispatchStats walk(const MessageView& view, OnPacket& on_packet, OnUnknown& on_unknown,
                             OnError& on_error) {
    DispatchStats stats;
    for (const auto& item : view) {
        if (!item) {
            ++stats.errors;
            on_error(item.error());
            break;
        }
        const auto decoded = decode_any<Set>(*item, view.direction());
        if (!decoded) {
            ++stats.errors;
            on_error(decoded.error());
            continue;
        }
        visit_alternative(*decoded, [&](const auto& p) {
            if constexpr (std::is_same_v<std::remove_cvref_t<decltype(p)>, UnknownPacket>) {
                ++stats.unknown;
                on_unknown(p);
            } else if (on_packet(p)) {
                ++stats.handled;
            } else {
                ++stats.ignored;
            }
        });
    }
    return stats;
}

} // namespace detail

/// Visit every packet of `view` in order. `visitor(const P&)` is called for each packet type P of
/// `Set` it accepts; other known packets are skipped (counted as ignored). If the visitor also
/// accepts `const UnknownPacket&` it receives unknown opcodes. `on_error(const Error&)` is
/// optional. No allocation.
// NOLINTBEGIN(cppcoreguidelines-missing-std-forward): both are called repeatedly, never moved.
template <class Set, class Visitor, class OnError = detail::IgnoreErrors>
constexpr DispatchStats dispatch(const MessageView& view, Visitor&& visitor,
                                 OnError&& on_error = {}) {
    auto on_packet = [&]([[maybe_unused]] const auto& p) {
        if constexpr (std::invocable<Visitor&, const std::remove_cvref_t<decltype(p)>&>) {
            static_cast<void>(visitor(p));
            return true;
        } else {
            return false;
        }
    };
    auto on_unknown = [&]([[maybe_unused]] const UnknownPacket& p) {
        if constexpr (std::invocable<Visitor&, const UnknownPacket&>) {
            static_cast<void>(visitor(p));
        }
    };
    return detail::walk<Set>(view, on_packet, on_unknown, on_error);
}
// NOLINTEND(cppcoreguidelines-missing-std-forward)

/// Handlers registered at run time, one slot per packet type of `Set`. Registration may allocate
/// (std::function); dispatching never does. Registering again replaces the handler. Do not
/// register from inside a handler. Replaces per-packet event processors.
template <class Set> class Dispatcher;

template <Packet... Ps> class Dispatcher<PacketSet<Ps...>> {
public:
    using SetType = PacketSet<Ps...>;

    /// Set (or replace) the handler for packets of type P.
    template <class P, class F>
        requires(SetType::template contains<P>) && std::invocable<F&, const P&>
    void on(F&& handler) {
        std::get<std::function<void(const P&)>>(slots_) = std::forward<F>(handler);
    }

    /// Handler for opcodes outside the set (the packet's bytes are valid during the call only).
    template <class F>
        requires std::invocable<F&, const UnknownPacket&>
    void on_unknown(F&& handler) {
        unknown_ = std::forward<F>(handler);
    }

    /// Handler for header and body decode errors.
    template <class F>
        requires std::invocable<F&, const Error&>
    void on_error(F&& handler) {
        error_ = std::forward<F>(handler);
    }

    /// Remove the handler for P (no-op if none).
    template <class P>
        requires(SetType::template contains<P>)
    void clear() {
        std::get<std::function<void(const P&)>>(slots_) = nullptr;
    }

    /// Dispatch every packet of `view` to the registered handlers.
    // NOLINTNEXTLINE(modernize-use-nodiscard): the stats are optional.
    DispatchStats dispatch(const MessageView& view) const {
        auto on_packet = [this](const auto& p) {
            const auto& slot =
                std::get<std::function<void(const std::remove_cvref_t<decltype(p)>&)>>(slots_);
            if (!slot) {
                return false;
            }
            slot(p);
            return true;
        };
        auto on_unknown = [this](const UnknownPacket& p) {
            if (unknown_) {
                unknown_(p);
            }
        };
        auto on_error = [this](const Error& e) {
            if (error_) {
                error_(e);
            }
        };
        return detail::walk<SetType>(view, on_packet, on_unknown, on_error);
    }

private:
    std::tuple<std::function<void(const Ps&)>...> slots_;
    std::function<void(const UnknownPacket&)> unknown_;
    std::function<void(const Error&)> error_;
};

} // namespace cigi
