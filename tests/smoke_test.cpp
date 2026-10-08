#include "cigi/version.hpp"

#include <gtest/gtest.h>

TEST(Version, ProtocolVersionIs33) {
    EXPECT_EQ(cigi::protocol_version_major, 3);
    EXPECT_EQ(cigi::protocol_version_minor, 3);
}

TEST(Version, LibraryVersionIsNonEmpty) {
    EXPECT_FALSE(cigi::library_version.empty());
    EXPECT_EQ(cigi::get_library_version(), cigi::library_version);
}
