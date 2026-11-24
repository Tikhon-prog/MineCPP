#pragma once

// A section's worth of values (for example 4096 block-state ids) stored as a
// small palette plus packed indices, like Minecraft's PalettedContainer.
//
//  - One palette entry: no index storage at all (a "uniform" container).
//  - More entries: indices live in a PackedStorage whose width is
//    max(ceil(log2(palette size)), min_bits), and grows when the palette does.
//
// The palette never shrinks on its own: overwriting the last use of a value
// leaves it in the palette, as in vanilla.
//
// Behaviour modelled on the MIT-licensed horizon-rust/horizon
// `PalettedContainer` (crates/horizon-world/src/chunk.rs). This is an
// independent C++ rewrite that stores plain 32-bit ids instead of NBT tags.

#include "packed_storage.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace minecpp {

class PalettedContainer {
public:
    using Value = std::uint32_t;

    // Every entry equals `value`.
    static PalettedContainer uniform(Value value, std::size_t entries, unsigned min_bits) {
        return PalettedContainer({value}, std::nullopt, entries, min_bits);
    }

    // Rebuilds a container from saved parts. `words` must be empty when the
    // palette has one entry, and every stored index must name a palette entry.
    static PalettedContainer from_parts(std::vector<Value> palette,
                                        std::vector<std::uint64_t> words,
                                        std::size_t entries,
                                        unsigned min_bits) {
        validate(entries, min_bits);
        if (palette.empty()) {
            throw std::invalid_argument("PalettedContainer: empty palette");
        }
        const unsigned bits = bits_for_palette(palette.size(), min_bits);
        if (bits == 0) {
            if (!words.empty()) {
                throw std::invalid_argument("PalettedContainer: uniform palette with data");
            }
            return PalettedContainer(std::move(palette), std::nullopt, entries, min_bits);
        }
        PackedStorage storage(bits, entries, std::move(words));
        for (std::size_t i = 0; i < entries; ++i) {
            if (storage.get(i) >= palette.size()) {
                throw std::invalid_argument("PalettedContainer: index outside palette");
            }
        }
        return PalettedContainer(std::move(palette), std::move(storage), entries, min_bits);
    }

    std::size_t size() const noexcept { return entries_; }
    const std::vector<Value>& palette() const noexcept { return palette_; }
    // Bits per stored index; 0 while the container is uniform.
    unsigned storage_bits() const noexcept { return storage_ ? storage_->bits() : 0U; }
    // Packed index words; empty while the container is uniform.
    const std::vector<std::uint64_t>& words() const noexcept {
        static const std::vector<std::uint64_t> kNone;
        return storage_ ? storage_->words() : kNone;
    }

    Value get(std::size_t index) const {
        check_index(index);
        if (!storage_) {
            return palette_[0];
        }
        return palette_[storage_->get(index)];
    }

    // Writes `value` and returns the value it replaced.
    Value set(std::size_t index, Value value) {
        check_index(index);
        if (!storage_) {
            if (palette_[0] == value) {
                return value;
            }
            const Value old = palette_[0];
            palette_.push_back(value);
            storage_.emplace(bits_for_palette(palette_.size(), min_bits_), entries_);
            storage_->set(index, 1U);
            return old;
        }

        std::size_t slot = palette_.size();
        for (std::size_t i = 0; i < palette_.size(); ++i) {
            if (palette_[i] == value) {
                slot = i;
                break;
            }
        }
        if (slot == palette_.size()) {
            palette_.push_back(value);
            const unsigned wanted = bits_for_palette(palette_.size(), min_bits_);
            if (wanted != storage_->bits()) {
                storage_ = storage_->repacked(wanted);
            }
        }
        const std::uint32_t old = storage_->set(index, static_cast<std::uint32_t>(slot));
        return palette_[old];
    }

private:
    PalettedContainer(std::vector<Value> palette,
                      std::optional<PackedStorage> storage,
                      std::size_t entries,
                      unsigned min_bits)
        : palette_(std::move(palette)), storage_(std::move(storage)), entries_(entries), min_bits_(min_bits) {
        validate(entries_, min_bits_);
    }

    static void validate(std::size_t entries, unsigned min_bits) {
        if (entries == 0) {
            throw std::invalid_argument("PalettedContainer: entries must be > 0");
        }
        if (min_bits < PackedStorage::kMinBits || min_bits > PackedStorage::kMaxBits) {
            throw std::invalid_argument("PalettedContainer: min_bits must be 1..32");
        }
    }

    static unsigned bits_for_palette(std::size_t length, unsigned min_bits) {
        if (length <= 1) {
            return 0;
        }
        const auto needed = static_cast<unsigned>(std::bit_width(length - 1));
        return needed > min_bits ? needed : min_bits;
    }

    void check_index(std::size_t index) const {
        if (index >= entries_) {
            throw std::out_of_range("PalettedContainer: index out of range");
        }
    }

    std::vector<Value> palette_;
    std::optional<PackedStorage> storage_;
    std::size_t entries_;
    unsigned min_bits_;
};

}  // namespace minecpp
