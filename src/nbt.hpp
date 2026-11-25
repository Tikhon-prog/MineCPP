#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace minecpp::nbt {

enum class Type : std::uint8_t {
    End = 0, Byte = 1, Short = 2, Int = 3, Long = 4, Float = 5,
    Double = 6, ByteArray = 7, String = 8, List = 9, Compound = 10,
    IntArray = 11, LongArray = 12
};

struct Limits {
    std::size_t maximum_input_bytes = 64U * 1024U * 1024U;
    std::size_t maximum_depth = 128;
    std::size_t maximum_tags = 1U * 1024U * 1024U;
    std::size_t maximum_array_elements = 16U * 1024U * 1024U;
};

struct Tag {
    using ByteArray = std::vector<std::int8_t>;
    using IntArray = std::vector<std::int32_t>;
    using LongArray = std::vector<std::int64_t>;
    using Scalar = std::variant<std::monostate, std::int8_t, std::int16_t,
        std::int32_t, std::int64_t, float, double, std::string,
        ByteArray, IntArray, LongArray>;

    Type type = Type::End;
    std::string name;
    Scalar value{};
    std::vector<Tag> children;

    [[nodiscard]] const Tag* find(std::string_view child_name) const noexcept;
    [[nodiscard]] const Tag& at(std::string_view child_name) const;
    [[nodiscard]] std::int8_t as_byte() const;
    [[nodiscard]] std::int16_t as_short() const;
    [[nodiscard]] std::int32_t as_int() const;
    [[nodiscard]] std::int64_t as_long() const;
    [[nodiscard]] float as_float() const;
    [[nodiscard]] double as_double() const;
    [[nodiscard]] const std::string& as_string() const;
    [[nodiscard]] const ByteArray& as_byte_array() const;
    [[nodiscard]] const IntArray& as_int_array() const;
    [[nodiscard]] const LongArray& as_long_array() const;
};

struct Document { Tag root; };

[[nodiscard]] Document parse(std::span<const std::uint8_t> bytes, const Limits& limits = {});
[[nodiscard]] std::vector<std::uint8_t> decompress(
    std::span<const std::uint8_t> compressed,
    std::size_t maximum_output_bytes = 64U * 1024U * 1024U);

} // namespace minecpp::nbt
