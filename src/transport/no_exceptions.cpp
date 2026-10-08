// Boost is built with BOOST_NO_EXCEPTIONS here (cigi_transport compiles with -fno-exceptions), in
// which case Boost requires the application to define boost::throw_exception. Nothing in the
// transport reaches it: every Asio call uses its error_code overload. If one ever did, aborting is
// the only sensible response without exceptions.

#include <boost/throw_exception.hpp>
#include <cstdlib>
#include <exception>

#ifdef BOOST_NO_EXCEPTIONS
namespace boost {

void throw_exception(const std::exception& /*e*/) {
    std::abort();
}

void throw_exception(const std::exception& /*e*/, const boost::source_location& /*loc*/) {
    std::abort();
}

} // namespace boost
#endif
