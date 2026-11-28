#pragma once

// MD5 (RFC 1321). Used only to derive the offline-mode player UUID the same way
// Minecraft does (UUID.nameUUIDFromBytes); not for security.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace minecpp {

inline std::array<std::uint8_t, 16> md5(std::string_view data) {
    static constexpr std::uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static constexpr unsigned S[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

    std::string message(data);
    const std::uint64_t bit_length = static_cast<std::uint64_t>(data.size()) * 8U;
    message.push_back(static_cast<char>(0x80));
    while (message.size() % 64U != 56U) message.push_back('\0');
    for (int i = 0; i < 8; ++i) {
        message.push_back(static_cast<char>((bit_length >> (8U * static_cast<unsigned>(i))) & 0xFFU));
    }

    std::uint32_t a0 = 0x67452301U;
    std::uint32_t b0 = 0xefcdab89U;
    std::uint32_t c0 = 0x98badcfeU;
    std::uint32_t d0 = 0x10325476U;

    for (std::size_t offset = 0; offset < message.size(); offset += 64U) {
        std::uint32_t m[16];
        for (std::size_t i = 0; i < 16; ++i) {
            const auto byte = [&](std::size_t k) {
                return static_cast<std::uint32_t>(static_cast<unsigned char>(message[offset + i * 4U + k]));
            };
            m[i] = byte(0) | (byte(1) << 8U) | (byte(2) << 16U) | (byte(3) << 24U);
        }
        std::uint32_t a = a0;
        std::uint32_t b = b0;
        std::uint32_t c = c0;
        std::uint32_t d = d0;
        for (unsigned i = 0; i < 64; ++i) {
            std::uint32_t f = 0;
            unsigned g = 0;
            if (i < 16) {
                f = (b & c) | (~b & d);
                g = i;
            } else if (i < 32) {
                f = (d & b) | (~d & c);
                g = (5U * i + 1U) % 16U;
            } else if (i < 48) {
                f = b ^ c ^ d;
                g = (3U * i + 5U) % 16U;
            } else {
                f = c ^ (b | ~d);
                g = (7U * i) % 16U;
            }
            f = f + a + K[i] + m[g];
            a = d;
            d = c;
            c = b;
            b = b + ((f << S[i]) | (f >> (32U - S[i])));
        }
        a0 += a;
        b0 += b;
        c0 += c;
        d0 += d;
    }

    std::array<std::uint8_t, 16> digest{};
    const std::uint32_t words[4] = {a0, b0, c0, d0};
    for (std::size_t w = 0; w < 4; ++w) {
        for (std::size_t k = 0; k < 4; ++k) {
            digest[w * 4U + k] = static_cast<std::uint8_t>((words[w] >> (8U * static_cast<unsigned>(k))) & 0xFFU);
        }
    }
    return digest;
}

// UUID Minecraft assigns to an offline-mode player: the version-3 UUID of
// "OfflinePlayer:<name>". Returned as 32 lowercase hex digits (no dashes), the
// form the game's --uuid argument takes.
inline std::string offline_player_uuid(const std::string& name) {
    std::array<std::uint8_t, 16> bytes = md5("OfflinePlayer:" + name);
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0FU) | 0x30U);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3FU) | 0x80U);
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    for (const std::uint8_t b : bytes) {
        out.push_back(kHex[b >> 4U]);
        out.push_back(kHex[b & 0x0FU]);
    }
    return out;
}

}  // namespace minecpp
