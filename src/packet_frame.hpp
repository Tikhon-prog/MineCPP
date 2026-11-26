#pragma once

// Packet framing on the wire.
//
// Uncompressed:  VarInt length | packet (VarInt id + payload)
// Compressed (a threshold >= 0 was negotiated):
//                VarInt length | VarInt data_length | body
//   data_length 0  -> body is the packet, not compressed (it was under the threshold)
//   data_length N  -> body is the zlib-compressed packet, N bytes once inflated
//
// try_decode_frame works on a receive buffer that may hold part of a frame or
// several frames: it returns nothing until a whole frame is present.

#include "protocol.hpp"
#include "region.hpp"  // zlib_compress / zlib_decompress

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace minecpp {

struct FrameConfig {
    // -1: compression not negotiated. Otherwise packets of at least this many
    // bytes are compressed.
    int compression_threshold = -1;
    std::size_t max_frame_bytes = 2097151;       // 2^21 - 1, vanilla's limit on a frame's length field
    std::size_t max_uncompressed = 8388608;      // 2^23, vanilla's limit on an inflated packet
};

inline std::vector<std::uint8_t> encode_frame(const std::vector<std::uint8_t>& packet, int compression_threshold) {
    PacketWriter body;
    if (compression_threshold < 0) {
        body.write_bytes(packet);
    } else if (packet.size() >= static_cast<std::size_t>(compression_threshold)) {
        body.write_varint(static_cast<std::int32_t>(packet.size()));
        body.write_bytes(zlib_compress(packet.data(), packet.size(), ZFormat::Zlib));
    } else {
        body.write_varint(0);
        body.write_bytes(packet);
    }
    PacketWriter frame;
    frame.write_varint(static_cast<std::int32_t>(body.bytes().size()));
    frame.write_bytes(body.bytes());
    return frame.take();
}

struct DecodedFrame {
    std::vector<std::uint8_t> packet;  // VarInt id + payload
    std::size_t consumed = 0;          // bytes of the buffer this frame used
};

namespace detail {

// Reads a VarInt from the front of a possibly incomplete buffer.
// nullopt: more bytes are needed. Throws when 5 bytes are all continuation bytes.
struct PeekedVarInt {
    std::int32_t value;
    std::size_t length;
};

inline std::optional<PeekedVarInt> peek_varint(const std::uint8_t* data, std::size_t size) {
    std::uint32_t result = 0;
    for (std::size_t i = 0; i < 5; ++i) {
        if (i >= size) {
            return std::nullopt;
        }
        result |= static_cast<std::uint32_t>(data[i] & 0x7F) << (7 * i);
        if ((data[i] & 0x80) == 0) {
            return PeekedVarInt{static_cast<std::int32_t>(result), i + 1};
        }
    }
    throw std::runtime_error("frame: VarInt is too big");
}

}  // namespace detail

inline std::optional<DecodedFrame> try_decode_frame(const std::uint8_t* data, std::size_t size,
                                                    const FrameConfig& config) {
    const auto prefix = detail::peek_varint(data, size);
    if (!prefix) {
        return std::nullopt;
    }
    if (prefix->value <= 0) {
        throw std::runtime_error("frame: invalid length");
    }
    const auto length = static_cast<std::size_t>(prefix->value);
    if (length > config.max_frame_bytes) {
        throw std::runtime_error("frame: length exceeds the limit");
    }
    if (size - prefix->length < length) {
        return std::nullopt;  // the rest has not arrived yet
    }

    const std::uint8_t* body = data + prefix->length;
    DecodedFrame out;
    out.consumed = prefix->length + length;

    if (config.compression_threshold < 0) {
        out.packet.assign(body, body + length);
        return out;
    }

    PacketReader reader(body, length);
    const std::int32_t data_length = reader.read_varint();
    if (data_length < 0) {
        throw std::runtime_error("frame: negative data length");
    }
    if (data_length == 0) {
        out.packet = reader.read_bytes(reader.remaining());
        if (out.packet.empty()) {
            throw std::runtime_error("frame: empty packet");
        }
        return out;
    }
    if (static_cast<std::size_t>(data_length) < static_cast<std::size_t>(config.compression_threshold)) {
        throw std::runtime_error("frame: compressed packet is below the threshold");
    }
    if (static_cast<std::size_t>(data_length) > config.max_uncompressed) {
        throw std::runtime_error("frame: uncompressed size exceeds the limit");
    }
    const std::uint8_t* compressed = reader.take(reader.remaining());
    const std::size_t compressed_size = static_cast<std::size_t>(body + length - compressed);
    out.packet = zlib_decompress(compressed, compressed_size, ZFormat::Zlib, static_cast<std::size_t>(data_length));
    if (out.packet.size() != static_cast<std::size_t>(data_length)) {
        throw std::runtime_error("frame: inflated size does not match the declared size");
    }
    return out;
}

}  // namespace minecpp
