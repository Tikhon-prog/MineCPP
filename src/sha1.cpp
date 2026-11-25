#include "sha1.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>

namespace minecpp {
namespace {

constexpr std::uint32_t rotate_left(std::uint32_t value, unsigned int amount) {
    return (value << amount) | (value >> (32U - amount));
}

class Sha1 {
public:
    void update(const unsigned char* data, std::size_t size) {
        total_bytes_ += static_cast<std::uint64_t>(size);
        while (size > 0) {
            const std::size_t count = std::min(size, buffer_.size() - buffered_);
            for (std::size_t i = 0; i < count; ++i) buffer_[buffered_ + i] = data[i];
            buffered_ += count;
            data += count;
            size -= count;
            if (buffered_ == buffer_.size()) {
                transform(buffer_.data());
                buffered_ = 0;
            }
        }
    }

    std::array<std::uint32_t, 5> finish() {
        const std::uint64_t bit_length = total_bytes_ * 8U;
        buffer_[buffered_++] = 0x80U;
        if (buffered_ > 56U) {
            while (buffered_ < 64U) buffer_[buffered_++] = 0U;
            transform(buffer_.data());
            buffered_ = 0;
        }
        while (buffered_ < 56U) buffer_[buffered_++] = 0U;
        for (int shift = 56; shift >= 0; shift -= 8)
            buffer_[buffered_++] = static_cast<unsigned char>((bit_length >> shift) & 0xffU);
        transform(buffer_.data());
        return state_;
    }

private:
    void transform(const unsigned char* block) {
        std::array<std::uint32_t, 80> words{};
        for (std::size_t i = 0; i < 16; ++i) {
            const std::size_t offset = i * 4U;
            words[i] = (static_cast<std::uint32_t>(block[offset]) << 24U) |
                       (static_cast<std::uint32_t>(block[offset + 1]) << 16U) |
                       (static_cast<std::uint32_t>(block[offset + 2]) << 8U) |
                       static_cast<std::uint32_t>(block[offset + 3]);
        }
        for (std::size_t i = 16; i < words.size(); ++i)
            words[i] = rotate_left(words[i - 3] ^ words[i - 8] ^
                                   words[i - 14] ^ words[i - 16], 1U);

        std::uint32_t a = state_[0], b = state_[1], c = state_[2];
        std::uint32_t d = state_[3], e = state_[4];
        for (std::size_t i = 0; i < 80; ++i) {
            std::uint32_t function = 0, constant = 0;
            if (i < 20) { function = (b & c) | ((~b) & d); constant = 0x5a827999U; }
            else if (i < 40) { function = b ^ c ^ d; constant = 0x6ed9eba1U; }
            else if (i < 60) { function = (b & c) | (b & d) | (c & d); constant = 0x8f1bbcdcU; }
            else { function = b ^ c ^ d; constant = 0xca62c1d6U; }
            const std::uint32_t temporary = rotate_left(a, 5U) + function + e + constant + words[i];
            e = d; d = c; c = rotate_left(b, 30U); b = a; a = temporary;
        }
        state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d; state_[4] += e;
    }

    std::array<std::uint32_t, 5> state_{
        0x67452301U, 0xefcdab89U, 0x98badcfeU, 0x10325476U, 0xc3d2e1f0U
    };
    std::array<unsigned char, 64> buffer_{};
    std::size_t buffered_ = 0;
    std::uint64_t total_bytes_ = 0;
};

} // namespace

std::string sha1_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open file for SHA-1: " + path.string());
    Sha1 hash;
    std::array<unsigned char, 64U * 1024U> buffer{};
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count > 0) hash.update(buffer.data(), static_cast<std::size_t>(count));
    }
    if (!input.eof()) throw std::runtime_error("Failed while reading file for SHA-1: " + path.string());
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(40);
    for (const std::uint32_t word : hash.finish())
        for (int shift = 28; shift >= 0; shift -= 4)
            result.push_back(hex[(word >> shift) & 0x0fU]);
    return result;
}

} // namespace minecpp
