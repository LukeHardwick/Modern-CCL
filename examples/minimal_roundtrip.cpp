// Build a Host -> IG message, parse it back, and print the decoded IG Control packet.
#include "cigi/message/builder.hpp"
#include "cigi/message/view.hpp"

#include <cstdlib>
#include <print>

// NOLINTNEXTLINE(bugprone-exception-escape): std::print may throw; an example may terminate.
int main() {
    cigi::StaticMessageBuilder<64> builder(cigi::Direction::HostToIg);
    const cigi::IgControl control{
        .database_number = 5,
        .ig_mode = cigi::IgMode::Operate,
        .timestamp_valid = true,
        .host_frame_number = 42,
        .timestamp = cigi::Timestamp{1000},
    };
    if (auto added = builder.add(control); !added) {
        std::println("add failed: {}", cigi::to_string(added.error().code));
        return EXIT_FAILURE;
    }
    std::println("built {} bytes", builder.size());

    const auto view = cigi::MessageView::parse(builder.bytes());
    if (!view) {
        std::println("parse failed: {}", cigi::to_string(view.error().code));
        return EXIT_FAILURE;
    }
    for (const auto& item : *view) {
        const auto decoded = item.and_then([](const cigi::RawPacket& raw) {
            return raw.decode_as<cigi::IgControl>();
        });
        if (!decoded) {
            std::println("decode failed: {}", cigi::to_string(decoded.error().code));
            return EXIT_FAILURE;
        }
        std::println("IG Control v{}.{}: database {}, mode {}, host frame {}, timestamp {} x 10us",
                     decoded->version.major, decoded->version.minor, decoded->database_number,
                     static_cast<int>(decoded->ig_mode), decoded->host_frame_number,
                     decoded->timestamp.count());
    }
    return EXIT_SUCCESS;
}
