#pragma once

// Java Edition network data types: big-endian integers, VarInt/VarLong,
// length-prefixed strings, packed block positions and UUIDs.
//
// PacketReader never reads past its buffer and every length is checked against
// the bytes that remain before any allocation, so hostile packets cannot make
// it allocate or loop unboundedly. All failures throw std::runtime_error.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace minecpp {

struct BlockPos {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    friend bool operator==(const BlockPos& a, const BlockPos& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
};

struct Uuid {
    std::uint64_t most = 0;
    std::uint64_t least = 0;
    friend bool operator==(const Uuid& a, const Uuid& b) { return a.most == b.most && a.least == b.least; }
};

// Position packing since 1.14: x (26 bits) | z (26 bits) | y (12 bits), signed.
constexpr std::uint64_t pack_position(const BlockPos& p) {
    return ((static_cast<std::uint64_t>(p.x) & 0x3FFFFFFULL) << 38) |
           ((static_cast<std::uint64_t>(p.z) & 0x3FFFFFFULL) << 12) | (static_cast<std::uint64_t>(p.y) & 0xFFFULL);
}

constexpr BlockPos unpack_position(std::uint64_t v) {
    const auto s = static_cast<std::int64_t>(v);
    BlockPos p;
    p.x = static_cast<std::int32_t>(s >> 38);
    p.y = static_cast<std::int32_t>(static_cast<std::int64_t>(v << 52) >> 52);
    p.z = static_cast<std::int32_t>(static_cast<std::int64_t>(v << 26) >> 38);
    return p;
}

// Vanilla limits a string to `max_chars` UTF-16 units and 3 bytes per unit.
constexpr std::size_t kDefaultMaxStringChars = 32767;

// Number of UTF-16 code units of a UTF-8 string, or -1 when it is not valid
// UTF-8 (overlong forms, surrogates, truncated or stray bytes).
inline long utf16_length_of_utf8(std::string_view s) {
    long units = 0;
    std::size_t i = 0;
    const std::size_t n = s.size();
    const auto byte = [&](std::size_t k) { return static_cast<unsigned>(static_cast<unsigned char>(s[k])); };
    while (i < n) {
        const unsigned b = byte(i);
        std::size_t len;
        unsigned cp;
        if (b < 0x80) {
            len = 1;
            cp = b;
        } else if (b >= 0xC2 && b <= 0xDF) {
            len = 2;
            cp = b & 0x1F;
        } else if (b >= 0xE0 && b <= 0xEF) {
            len = 3;
            cp = b & 0x0F;
        } else if (b >= 0xF0 && b <= 0xF4) {
            len = 4;
            cp = b & 0x07;
        } else {
            return -1;
        }
        if (i + len > n) {
            return -1;
        }
        for (std::size_t k = 1; k < len; ++k) {
            const unsigned c = byte(i + k);
            if ((c & 0xC0) != 0x80) {
                return -1;
            }
            cp = (cp << 6) | (c & 0x3F);
        }
        if ((len == 3 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) ||
            (len == 4 && (cp < 0x10000 || cp > 0x10FFFF))) {
            return -1;
        }
        units += cp > 0xFFFF ? 2 : 1;
        i += len;
    }
    return units;
}

class PacketWriter {
public:
    const std::vector<std::uint8_t>& bytes() const noexcept { return out_; }
    std::vector<std::uint8_t> take() { return std::move(out_); }

    void write_u8(std::uint8_t v) { out_.push_back(v); }
    void write_i8(std::int8_t v) { out_.push_back(static_cast<std::uint8_t>(v)); }
    void write_bool(bool v) { out_.push_back(v ? 1 : 0); }
    void write_u16(std::uint16_t v) { write_be(v, 2); }
    void write_i16(std::int16_t v) { write_be(static_cast<std::uint16_t>(v), 2); }
    void write_i32(std::int32_t v) { write_be(static_cast<std::uint32_t>(v), 4); }
    void write_i64(std::int64_t v) { write_be(static_cast<std::uint64_t>(v), 8); }
    void write_f32(float v) {
        std::uint32_t bits;
        std::memcpy(&bits, &v, sizeof bits);
        write_be(bits, 4);
    }
    void write_f64(double v) {
        std::uint64_t bits;
        std::memcpy(&bits, &v, sizeof bits);
        write_be(bits, 8);
    }

    void write_varint(std::int32_t value) {
        auto v = static_cast<std::uint32_t>(value);
        while (v >= 0x80) {
            out_.push_back(static_cast<std::uint8_t>((v & 0x7F) | 0x80));
            v >>= 7;
        }
        out_.push_back(static_cast<std::uint8_t>(v));
    }

    void write_varlong(std::int64_t value) {
        auto v = static_cast<std::uint64_t>(value);
        while (v >= 0x80) {
            out_.push_back(static_cast<std::uint8_t>((v & 0x7F) | 0x80));
            v >>= 7;
        }
        out_.push_back(static_cast<std::uint8_t>(v));
    }

    void write_string(std::string_view s, std::size_t max_chars = kDefaultMaxStringChars) {
        if (s.size() > max_chars * 3) {
            throw std::invalid_argument("PacketWriter: string too long");
        }
        const long units = utf16_length_of_utf8(s);
        if (units < 0) {
            throw std::invalid_argument("PacketWriter: string is not valid UTF-8");
        }
        if (static_cast<std::size_t>(units) > max_chars) {
            throw std::invalid_argument("PacketWriter: string has too many characters");
        }
        write_varint(static_cast<std::int32_t>(s.size()));
        out_.insert(out_.end(), s.begin(), s.end());
    }

    void write_uuid(const Uuid& u) {
        write_be(u.most, 8);
        write_be(u.least, 8);
    }

    void write_position(const BlockPos& p) { write_be(pack_position(p), 8); }

    void write_bytes(const std::uint8_t* data, std::size_t n) { out_.insert(out_.end(), data, data + n); }
    void write_bytes(const std::vector<std::uint8_t>& v) { write_bytes(v.data(), v.size()); }

    // VarInt length, then the bytes.
    void write_byte_array(const std::vector<std::uint8_t>& v) {
        write_varint(static_cast<std::int32_t>(v.size()));
        write_bytes(v);
    }

private:
    void write_be(std::uint64_t v, int bytes) {
        for (int shift = (bytes - 1) * 8; shift >= 0; shift -= 8) {
            out_.push_back(static_cast<std::uint8_t>((v >> shift) & 0xFF));
        }
    }

    std::vector<std::uint8_t> out_;
};

class PacketReader {
public:
    PacketReader(const std::uint8_t* data, std::size_t size) : p_(data), end_(data + size) {}
    explicit PacketReader(const std::vector<std::uint8_t>& v) : PacketReader(v.data(), v.size()) {}

    std::size_t remaining() const noexcept { return static_cast<std::size_t>(end_ - p_); }
    bool empty() const noexcept { return p_ == end_; }

    std::uint8_t read_u8() {
        need(1);
        return *p_++;
    }
    std::int8_t read_i8() { return static_cast<std::int8_t>(read_u8()); }
    bool read_bool() {
        const std::uint8_t v = read_u8();
        if (v > 1) {
            throw std::runtime_error("PacketReader: invalid boolean");
        }
        return v == 1;
    }
    std::uint16_t read_u16() { return static_cast<std::uint16_t>(read_be(2)); }
    std::int16_t read_i16() { return static_cast<std::int16_t>(read_be(2)); }
    std::int32_t read_i32() { return static_cast<std::int32_t>(read_be(4)); }
    std::int64_t read_i64() { return static_cast<std::int64_t>(read_be(8)); }
    float read_f32() {
        const auto bits = static_cast<std::uint32_t>(read_be(4));
        float f;
        std::memcpy(&f, &bits, sizeof f);
        return f;
    }
    double read_f64() {
        const std::uint64_t bits = read_be(8);
        double d;
        std::memcpy(&d, &bits, sizeof d);
        return d;
    }

    // At most 5 bytes; a sixth continuation byte is an error.
    std::int32_t read_varint() {
        std::uint32_t result = 0;
        for (int i = 0; i < 5; ++i) {
            const std::uint8_t b = read_u8();
            result |= static_cast<std::uint32_t>(b & 0x7F) << (7 * i);
            if ((b & 0x80) == 0) {
                return static_cast<std::int32_t>(result);
            }
        }
        throw std::runtime_error("PacketReader: VarInt is too big");
    }

    // At most 10 bytes.
    std::int64_t read_varlong() {
        std::uint64_t result = 0;
        for (int i = 0; i < 10; ++i) {
            const std::uint8_t b = read_u8();
            result |= static_cast<std::uint64_t>(b & 0x7F) << (7 * i);
            if ((b & 0x80) == 0) {
                return static_cast<std::int64_t>(result);
            }
        }
        throw std::runtime_error("PacketReader: VarLong is too big");
    }

    std::string read_string(std::size_t max_chars = kDefaultMaxStringChars) {
        const std::int32_t len = read_varint();
        if (len < 0 || static_cast<std::size_t>(len) > max_chars * 3) {
            throw std::runtime_error("PacketReader: string length out of range");
        }
        const std::uint8_t* bytes = take(static_cast<std::size_t>(len));
        std::string s(reinterpret_cast<const char*>(bytes), static_cast<std::size_t>(len));
        const long units = utf16_length_of_utf8(s);
        if (units < 0) {
            throw std::runtime_error("PacketReader: string is not valid UTF-8");
        }
        if (static_cast<std::size_t>(units) > max_chars) {
            throw std::runtime_error("PacketReader: string has too many characters");
        }
        return s;
    }

    Uuid read_uuid() {
        Uuid u;
        u.most = read_be(8);
        u.least = read_be(8);
        return u;
    }

    BlockPos read_position() { return unpack_position(read_be(8)); }

    const std::uint8_t* take(std::size_t n) {
        need(n);
        const std::uint8_t* start = p_;
        p_ += n;
        return start;
    }

    std::vector<std::uint8_t> read_bytes(std::size_t n) {
        const std::uint8_t* start = take(n);
        return std::vector<std::uint8_t>(start, start + n);
    }

    // VarInt length (at most `max_len`), then the bytes.
    std::vector<std::uint8_t> read_byte_array(std::size_t max_len) {
        const std::int32_t len = read_varint();
        if (len < 0 || static_cast<std::size_t>(len) > max_len) {
            throw std::runtime_error("PacketReader: byte array length out of range");
        }
        return read_bytes(static_cast<std::size_t>(len));
    }

private:
    void need(std::size_t n) const {
        if (remaining() < n) {
            throw std::runtime_error("PacketReader: unexpected end of packet");
        }
    }

    std::uint64_t read_be(int bytes) {
        need(static_cast<std::size_t>(bytes));
        std::uint64_t v = 0;
        for (int i = 0; i < bytes; ++i) {
            v = (v << 8) | *p_++;
        }
        return v;
    }

    const std::uint8_t* p_;
    const std::uint8_t* end_;
};

}  // namespace minecpp
