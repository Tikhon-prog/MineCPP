#include "packet_frame.hpp"
#include "protocol.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace minecpp;

#define CHECK(expression) do { if (!(expression)) throw std::runtime_error("Test failed: " #expression); } while (false)

template <typename Fn>
static bool throws(Fn&& fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

using Bytes = std::vector<std::uint8_t>;

static Bytes varint_bytes(std::int32_t v) {
    PacketWriter w;
    w.write_varint(v);
    return w.take();
}

int main() {
    // VarInt reference values.
    {
        const std::vector<std::pair<std::int32_t, Bytes>> cases = {
            {0, {0x00}},
            {1, {0x01}},
            {2, {0x02}},
            {127, {0x7F}},
            {128, {0x80, 0x01}},
            {255, {0xFF, 0x01}},
            {25565, {0xDD, 0xC7, 0x01}},
            {2097151, {0xFF, 0xFF, 0x7F}},
            {2147483647, {0xFF, 0xFF, 0xFF, 0xFF, 0x07}},
            {-1, {0xFF, 0xFF, 0xFF, 0xFF, 0x0F}},
            {-2147483647 - 1, {0x80, 0x80, 0x80, 0x80, 0x08}},
        };
        for (const auto& [value, bytes] : cases) {
            CHECK(varint_bytes(value) == bytes);
            PacketReader r(bytes);
            CHECK(r.read_varint() == value);
            CHECK(r.empty());
        }
        // Too long: six continuation bytes.
        const Bytes too_big = {0x80, 0x80, 0x80, 0x80, 0x80, 0x01};
        CHECK(throws([&] { PacketReader r(too_big); (void)r.read_varint(); }));
        // Truncated.
        const Bytes cut = {0x80, 0x80};
        CHECK(throws([&] { PacketReader r(cut); (void)r.read_varint(); }));
    }

    // VarLong reference values.
    {
        const std::vector<std::pair<std::int64_t, Bytes>> cases = {
            {0, {0x00}},
            {127, {0x7F}},
            {128, {0x80, 0x01}},
            {2147483647, {0xFF, 0xFF, 0xFF, 0xFF, 0x07}},
            {9223372036854775807LL, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F}},
            {-1, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01}},
            {INT64_MIN, {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x01}},
        };
        for (const auto& [value, bytes] : cases) {
            PacketWriter w;
            w.write_varlong(value);
            CHECK(w.bytes() == bytes);
            PacketReader r(bytes);
            CHECK(r.read_varlong() == value);
        }
        const Bytes too_big(11, 0x80);
        CHECK(throws([&] { PacketReader r(too_big); (void)r.read_varlong(); }));
    }

    // Fixed-width values are big-endian and round-trip.
    {
        PacketWriter w;
        w.write_i32(0x01020304);
        w.write_u16(0xABCD);
        const Bytes expected = {0x01, 0x02, 0x03, 0x04, 0xAB, 0xCD};
        CHECK(w.bytes() == expected);

        PacketWriter all;
        all.write_u8(200);
        all.write_i8(-5);
        all.write_bool(true);
        all.write_i16(-12345);
        all.write_i32(-1234567890);
        all.write_i64(-1234567890123456789LL);
        all.write_f32(3.5F);
        all.write_f64(-0.125);
        all.write_uuid(Uuid{0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL});
        PacketReader r(all.bytes());
        CHECK(r.read_u8() == 200);
        CHECK(r.read_i8() == -5);
        CHECK(r.read_bool());
        CHECK(r.read_i16() == -12345);
        CHECK(r.read_i32() == -1234567890);
        CHECK(r.read_i64() == -1234567890123456789LL);
        CHECK(r.read_f32() == 3.5F);
        CHECK(r.read_f64() == -0.125);
        const Uuid u = r.read_uuid();
        CHECK(u.most == 0x0123456789ABCDEFULL && u.least == 0xFEDCBA9876543210ULL);
        CHECK(r.empty());
        CHECK(throws([&] { (void)r.read_u8(); }));

        const Bytes bad_bool = {2};
        CHECK(throws([&] { PacketReader b(bad_bool); (void)b.read_bool(); }));
    }

    // Strings.
    {
        PacketWriter w;
        w.write_string("hello");
        const Bytes expected = {0x05, 'h', 'e', 'l', 'l', 'o'};
        CHECK(w.bytes() == expected);
        PacketReader r(w.bytes());
        CHECK(r.read_string() == "hello");

        // Multi-byte text, and an emoji that counts as two UTF-16 units.
        const std::string text = "h\xC3\xA9llo \xE2\x82\xAC";
        PacketWriter t;
        t.write_string(text);
        PacketReader tr(t.bytes());
        CHECK(tr.read_string() == text);

        const std::string emoji = "\xF0\x9F\x98\x80";
        PacketWriter e;
        e.write_string(emoji, 2);
        CHECK(throws([&] { PacketWriter x; x.write_string(emoji, 1); }));
        PacketReader er(e.bytes());
        CHECK(er.read_string(2) == emoji);
        PacketReader er1(e.bytes());
        CHECK(throws([&] { (void)er1.read_string(1); }));

        // Hostile strings: bad UTF-8, huge or negative length, truncated.
        const std::vector<Bytes> bad = {
            {0x01, 0xFF},                    // invalid byte
            {0x02, 0xC0, 0x80},              // overlong NUL
            {0x03, 0xED, 0xA0, 0x80},        // encoded surrogate
            {0x02, 0xC3},                    // truncated sequence
            {0x05, 'a', 'b'},                // length beyond data
            {0xFF, 0xFF, 0xFF, 0xFF, 0x0F},  // negative length
            {0xFF, 0xFF, 0xFF, 0xFF, 0x07},  // 2 GiB
        };
        for (const Bytes& b : bad) {
            CHECK(throws([&] { PacketReader x(b); (void)x.read_string(); }));
        }
        // A byte length over 3 * max_chars is refused before the bytes are looked at.
        PacketWriter big;
        big.write_varint(100);
        CHECK(throws([&] { PacketReader x(big.bytes()); (void)x.read_string(10); }));
    }

    // Positions.
    {
        // x = 18357644, y = 831, z = -20882616, worked out from the layout:
        // x << 38 | (z & 0x3FFFFFF) << 12 | (y & 0xFFF).
        const BlockPos p{18357644, 831, -20882616};
        CHECK(pack_position(p) == 0x4607632C15B4833FULL);
        CHECK(unpack_position(0x4607632C15B4833FULL) == p);

        const BlockPos corners[] = {{0, 0, 0},          {-1, -1, -1},          {33554431, 2047, 33554431},
                                    {-33554432, -2048, -33554432}, {1, -64, -1}, {-30000000, 319, 30000000}};
        for (const BlockPos& c : corners) {
            CHECK(unpack_position(pack_position(c)) == c);
            PacketWriter w;
            w.write_position(c);
            CHECK(w.bytes().size() == 8);
            PacketReader r(w.bytes());
            CHECK(r.read_position() == c);
        }
    }

    // Byte arrays.
    {
        PacketWriter w;
        w.write_byte_array({1, 2, 3});
        PacketReader r(w.bytes());
        CHECK((r.read_byte_array(10) == Bytes{1, 2, 3}));
        PacketReader r2(w.bytes());
        CHECK(throws([&] { (void)r2.read_byte_array(2); }));
    }

    // Uncompressed frames.
    {
        const Bytes packet = {0x00, 0x01, 0x02, 0x03};
        const Bytes frame = encode_frame(packet, -1);
        const Bytes expected = {0x04, 0x00, 0x01, 0x02, 0x03};
        CHECK(frame == expected);

        FrameConfig cfg;
        // Every strict prefix of a frame is "not yet".
        for (std::size_t cut = 0; cut < frame.size(); ++cut) {
            CHECK(!try_decode_frame(frame.data(), cut, cfg).has_value());
        }
        const auto whole = try_decode_frame(frame.data(), frame.size(), cfg);
        CHECK(whole.has_value() && whole->packet == packet && whole->consumed == frame.size());

        // Two frames back to back are taken one at a time.
        Bytes two = frame;
        const Bytes second = encode_frame({0x09}, -1);
        two.insert(two.end(), second.begin(), second.end());
        const auto first = try_decode_frame(two.data(), two.size(), cfg);
        CHECK(first && first->consumed == frame.size());
        const auto next = try_decode_frame(two.data() + first->consumed, two.size() - first->consumed, cfg);
        CHECK(next && next->packet == Bytes{0x09});

        // Limits and malformed lengths.
        CHECK(throws([&] { const Bytes zero = {0x00}; (void)try_decode_frame(zero.data(), zero.size(), cfg); }));
        CHECK(throws([&] {
            const Bytes huge = {0xFF, 0xFF, 0xFF, 0x7F};  // 268435455 > 2^21 - 1
            (void)try_decode_frame(huge.data(), huge.size(), cfg);
        }));
        CHECK(throws([&] {
            const Bytes endless = {0x80, 0x80, 0x80, 0x80, 0x80};
            (void)try_decode_frame(endless.data(), endless.size(), cfg);
        }));
    }

    // Compressed frames.
    {
        FrameConfig cfg;
        cfg.compression_threshold = 64;

        // Below the threshold: sent as-is behind a zero data length.
        const Bytes small = {0x01, 0x02, 0x03};
        const Bytes small_frame = encode_frame(small, 64);
        const Bytes small_expected = {0x04, 0x00, 0x01, 0x02, 0x03};
        CHECK(small_frame == small_expected);
        const auto s = try_decode_frame(small_frame.data(), small_frame.size(), cfg);
        CHECK(s && s->packet == small);

        // At or over the threshold: compressed, with the inflated size declared.
        Bytes big(500);
        for (std::size_t i = 0; i < big.size(); ++i) {
            big[i] = static_cast<std::uint8_t>(i % 7);
        }
        const Bytes big_frame = encode_frame(big, 64);
        CHECK(big_frame.size() < big.size());
        const auto b = try_decode_frame(big_frame.data(), big_frame.size(), cfg);
        CHECK(b && b->packet == big && b->consumed == big_frame.size());
        for (std::size_t cut = 0; cut < big_frame.size(); ++cut) {
            CHECK(!try_decode_frame(big_frame.data(), cut, cfg).has_value());
        }

        // Exactly the threshold counts as compressed.
        const Bytes edge(64, 0x05);
        const Bytes edge_frame = encode_frame(edge, 64);
        const auto e = try_decode_frame(edge_frame.data(), edge_frame.size(), cfg);
        CHECK(e && e->packet == edge);

        // Hand-built malformed frames.
        const auto frame_of = [](std::int32_t declared, const Bytes& zdata) {
            PacketWriter body;
            body.write_varint(declared);
            body.write_bytes(zdata);
            PacketWriter frame;
            frame.write_varint(static_cast<std::int32_t>(body.bytes().size()));
            frame.write_bytes(body.bytes());
            return frame.take();
        };
        const Bytes z = zlib_compress(big.data(), big.size(), ZFormat::Zlib);

        // Declared size smaller than the data really inflates to (a bomb in miniature).
        const Bytes lies_small = frame_of(100, z);
        CHECK(throws([&] { (void)try_decode_frame(lies_small.data(), lies_small.size(), cfg); }));
        // Declared size larger than the data inflates to.
        const Bytes lies_big = frame_of(900, z);
        CHECK(throws([&] { (void)try_decode_frame(lies_big.data(), lies_big.size(), cfg); }));
        // Declared size under the threshold but non-zero.
        const Bytes under = frame_of(10, z);
        CHECK(throws([&] { (void)try_decode_frame(under.data(), under.size(), cfg); }));
        // Declared size over the global limit.
        const Bytes over = frame_of(0x7FFFFFFF, z);
        CHECK(throws([&] { (void)try_decode_frame(over.data(), over.size(), cfg); }));
        // Corrupt zlib data.
        const Bytes junk = frame_of(500, Bytes{1, 2, 3, 4, 5, 6});
        CHECK(throws([&] { (void)try_decode_frame(junk.data(), junk.size(), cfg); }));
        // Compressed frame with no payload at all.
        const Bytes empty_body = frame_of(500, Bytes{});
        CHECK(throws([&] { (void)try_decode_frame(empty_body.data(), empty_body.size(), cfg); }));
        // Zero data length followed by nothing: an empty packet.
        const Bytes empty_packet = frame_of(0, Bytes{});
        CHECK(throws([&] { (void)try_decode_frame(empty_packet.data(), empty_packet.size(), cfg); }));
    }

    std::cout << "protocol tests passed\n";
    return 0;
}
