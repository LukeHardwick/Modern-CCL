// Boost is built with BOOST_NO_EXCEPTIONS here (cigi_transport compiles with -fno-exceptions), in
// which case Boost requires the application to define boost::throw_exception. Ordinary socket
// errors never reach it (every Asio call uses its error_code overload); what does is rare OS
// failure inside Asio, e.g. io_context creation when out of file descriptors. Without exceptions
// the only sensible response is to report and abort.
//
// Note: the definitions cannot be marked [[noreturn]]; Boost's own declaration is not, and clang
// rejects the attribute on a later redeclaration. They never return anyway.
//
// Coverage: only reachable through a death test (the aborting child writes no profile), so this
// file is excluded from the coverage report (cmake/Coverage.cmake).

#include <boost/throw_exception.hpp>
#include <cstdlib>
#include <exception>
#include <print>

#ifdef BOOST_NO_EXCEPTIONS
namespace boost {

void throw_exception(const std::exception& e) {
    std::println(stderr, "cigi_transport: fatal error inside Boost: {}", e.what());
    std::abort();
}

void throw_exception(const std::exception& e, const boost::source_location& loc) {
    std::println(stderr, "cigi_transport: fatal error inside Boost ({}:{}): {}", loc.file_name(),
                 loc.line(), e.what());
    std::abort();
}

} // namespace boost
#endif
