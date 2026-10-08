#include "cigi/error.hpp"

namespace cigi {

std::string_view to_string(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::BufferTooSmall:
        return "BufferTooSmall";
    case ErrorCode::Truncated:
        return "Truncated";
    case ErrorCode::InvalidPacketSize:
        return "InvalidPacketSize";
    case ErrorCode::InvalidByteSwapMagic:
        return "InvalidByteSwapMagic";
    case ErrorCode::InvalidFieldValue:
        return "InvalidFieldValue";
    case ErrorCode::UnexpectedFirstPacket:
        return "UnexpectedFirstPacket";
    case ErrorCode::UnsupportedVersion:
        return "UnsupportedVersion";
    case ErrorCode::UnknownOpcode:
        return "UnknownOpcode";
    case ErrorCode::WrongDirection:
        return "WrongDirection";
    case ErrorCode::DuplicateFramePacket:
        return "DuplicateFramePacket";
    case ErrorCode::OpcodeMismatch:
        return "OpcodeMismatch";
    }
    return "Unknown";
}

} // namespace cigi
