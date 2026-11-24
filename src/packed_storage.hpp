#pragma once

// Palette indices packed into padded 64-bit words, as in Minecraft's chunk
// section storage (1.16+): an entry never spans two words, so each word holds
// floor(64 / bits) entries and the remaining high bits are padding.
//
// Behaviour modelled on the MIT-licensed horizon-rust/horizon `PackedStorage`
// (crates/horizon-core/src/storage.rs). This is an independent C++ rewrite.

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace minecpp {

class PackedStorage {
public:
    static constexpr unsigned kMinBits = 1;
    static constexpr unsigned kMaxBits = 32;

    // Zero-filled storage of `length` entries, `bits` bits each.
    PackedStorage(unsigned bits, std::size_t length)
        : PackedStorage(bits, length, std::vector<std::uint64_t>(words_for(bits, length), 0U)) {}

    // Adopts existing words (for example read from a saved chunk). Padding and
    // unused tail bits are kept exactly as given, like Java's long[].
    PackedStorage(unsigned bits, std::size_t length, std::vector<std::uint64_t> words)
        : bits_(bits), length_(length), per_word_(per_word(bits)), words_(std::move(words)) {
        if (words_.size() != words_for(bits, length)) {
            throw std::invalid_argument("PackedStorage: wrong word count");
        }
        mask_ = (std::uint64_t{1} << bits_) - 1U;
    }

    unsigned bits() const noexcept { return bits_; }
    std::size_t size() const noexcept { return length_; }
    bool empty() const noexcept { return length_ == 0; }
    const std::vector<std::uint64_t>& words() const noexcept { return words_; }

    std::uint32_t get(std::size_t index) const {
        check_index(index);
        const auto [word, shift] = locate(index);
        return static_cast<std::uint32_t>((words_[word] >> shift) & mask_);
    }

    // Writes `value` and returns the entry it replaced.
    std::uint32_t set(std::size_t index, std::uint32_t value) {
        check_index(index);
        if (static_cast<std::uint64_t>(value) > mask_) {
            throw std::invalid_argument("PackedStorage: value does not fit in bits");
        }
        const auto [word, shift] = locate(index);
        const std::uint64_t old = (words_[word] >> shift) & mask_;
        words_[word] = (words_[word] & ~(mask_ << shift)) | (static_cast<std::uint64_t>(value) << shift);
        return static_cast<std::uint32_t>(old);
    }

    // Copies into storage with a different width (a palette growing or
    // shrinking). Every value must fit the new width.
    PackedStorage repacked(unsigned new_bits) const {
        PackedStorage out(new_bits, length_);
        for (std::size_t i = 0; i < length_; ++i) {
            out.set(i, get(i));
        }
        return out;
    }

    friend bool operator==(const PackedStorage& a, const PackedStorage& b) {
        return a.bits_ == b.bits_ && a.length_ == b.length_ && a.words_ == b.words_;
    }

private:
    static std::size_t per_word(unsigned bits) {
        if (bits < kMinBits || bits > kMaxBits) {
            throw std::invalid_argument("PackedStorage: bits must be 1..32");
        }
        return 64U / bits;
    }

    static std::size_t words_for(unsigned bits, std::size_t length) {
        const std::size_t per = per_word(bits);
        return (length + per - 1U) / per;
    }

    std::pair<std::size_t, unsigned> locate(std::size_t index) const noexcept {
        return {index / per_word_, static_cast<unsigned>((index % per_word_) * bits_)};
    }

    void check_index(std::size_t index) const {
        if (index >= length_) {
            throw std::out_of_range("PackedStorage: index out of range");
        }
    }

    unsigned bits_;
    std::size_t length_;
    std::size_t per_word_;
    std::uint64_t mask_ = 0;
    std::vector<std::uint64_t> words_;
};

}  // namespace minecpp
