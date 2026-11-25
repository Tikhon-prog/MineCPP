#include "nbt.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <zlib.h>

namespace minecpp::nbt {
namespace {

class Reader {
public:
    Reader(std::span<const std::uint8_t> input, const Limits& limits)
        : input_(input), limits_(limits) {
        if (input.size() > limits.maximum_input_bytes)
            throw std::runtime_error("NBT input exceeds the configured byte limit");
    }

    Document read_document() {
        if (read_type() != Type::Compound) fail("root tag must be a compound");
        Tag root;
        root.type = Type::Compound;
        root.name = read_string();
        read_payload(root, 0);
        if (position_ != input_.size()) fail("unexpected trailing bytes");
        return {std::move(root)};
    }

private:
    [[noreturn]] void fail(const char* message) const {
        throw std::runtime_error(std::string("Invalid NBT at byte ") +
                                 std::to_string(position_) + ": " + message);
    }

    std::uint8_t read_u8() {
        if (position_ >= input_.size()) fail("unexpected end of input");
        return input_[position_++];
    }

    std::uint16_t read_u16() {
        const std::uint16_t high = read_u8();
        return static_cast<std::uint16_t>((high << 8U) | read_u8());
    }

    std::uint32_t read_u32() {
        const std::uint32_t a = read_u8();
        const std::uint32_t b = read_u8();
        const std::uint32_t c = read_u8();
        const std::uint32_t d = read_u8();
        return (a << 24U) | (b << 16U) | (c << 8U) | d;
    }

    std::uint64_t read_u64() {
        return (static_cast<std::uint64_t>(read_u32()) << 32U) | read_u32();
    }

    Type read_type() {
        const std::uint8_t raw = read_u8();
        if (raw > static_cast<std::uint8_t>(Type::LongArray)) fail("unknown tag type");
        return static_cast<Type>(raw);
    }

    std::string read_string() {
        const std::size_t length = read_u16();
        if (length > input_.size() - position_) fail("string length exceeds input");
        std::string result;
        result.reserve(length);
        for (std::size_t i = 0; i < length; ++i)
            result.push_back(static_cast<char>(read_u8()));
        return result;
    }

    std::size_t read_length() {
        const std::int32_t length = std::bit_cast<std::int32_t>(read_u32());
        if (length < 0) fail("negative collection length");
        const auto count = static_cast<std::size_t>(length);
        if (count > limits_.maximum_array_elements) fail("collection exceeds element limit");
        return count;
    }

    void count_tag() {
        ++tag_count_;
        if (tag_count_ > limits_.maximum_tags) fail("maximum tag count exceeded");
    }

    void read_payload(Tag& tag, std::size_t depth) {
        if (depth > limits_.maximum_depth) fail("maximum nesting depth exceeded");
        count_tag();

        switch (tag.type) {
            case Type::End:
                fail("TAG_End has no payload");
            case Type::Byte:
                tag.value = std::bit_cast<std::int8_t>(read_u8());
                break;
            case Type::Short:
                tag.value = std::bit_cast<std::int16_t>(read_u16());
                break;
            case Type::Int:
                tag.value = std::bit_cast<std::int32_t>(read_u32());
                break;
            case Type::Long:
                tag.value = std::bit_cast<std::int64_t>(read_u64());
                break;
            case Type::Float:
                tag.value = std::bit_cast<float>(read_u32());
                break;
            case Type::Double:
                tag.value = std::bit_cast<double>(read_u64());
                break;
            case Type::ByteArray: {
                const std::size_t count = read_length();
                if (count > input_.size() - position_) fail("byte array exceeds input");
                Tag::ByteArray values;
                values.reserve(count);
                for (std::size_t i = 0; i < count; ++i)
                    values.push_back(std::bit_cast<std::int8_t>(read_u8()));
                tag.value = std::move(values);
                break;
            }
            case Type::String:
                tag.value = read_string();
                break;
            case Type::List: {
                const Type element_type = read_type();
                const std::size_t count = read_length();
                if (element_type == Type::End && count != 0)
                    fail("non-empty list cannot use TAG_End elements");
                if (count > limits_.maximum_tags - std::min(tag_count_, limits_.maximum_tags))
                    fail("list exceeds remaining tag budget");
                tag.children.reserve(count);
                for (std::size_t i = 0; i < count; ++i) {
                    Tag child;
                    child.type = element_type;
                    read_payload(child, depth + 1);
                    tag.children.push_back(std::move(child));
                }
                break;
            }
            case Type::Compound: {
                std::unordered_set<std::string> names;
                while (true) {
                    const Type child_type = read_type();
                    if (child_type == Type::End) break;
                    Tag child;
                    child.type = child_type;
                    child.name = read_string();
                    if (!names.insert(child.name).second)
                        fail("compound contains duplicate tag names");
                    read_payload(child, depth + 1);
                    tag.children.push_back(std::move(child));
                }
                break;
            }
            case Type::IntArray: {
                const std::size_t count = read_length();
                if (count > (input_.size() - position_) / 4U)
                    fail("int array exceeds input");
                Tag::IntArray values;
                values.reserve(count);
                for (std::size_t i = 0; i < count; ++i)
                    values.push_back(std::bit_cast<std::int32_t>(read_u32()));
                tag.value = std::move(values);
                break;
            }
            case Type::LongArray: {
                const std::size_t count = read_length();
                if (count > (input_.size() - position_) / 8U)
                    fail("long array exceeds input");
                Tag::LongArray values;
                values.reserve(count);
                for (std::size_t i = 0; i < count; ++i)
                    values.push_back(std::bit_cast<std::int64_t>(read_u64()));
                tag.value = std::move(values);
                break;
            }
        }
    }

    std::span<const std::uint8_t> input_;
    const Limits& limits_;
    std::size_t position_ = 0;
    std::size_t tag_count_ = 0;
};

} // namespace

const Tag* Tag::find(std::string_view name) const noexcept {
    for (const Tag& child : children)
        if (child.name == name) return &child;
    return nullptr;
}

const Tag& Tag::at(std::string_view name) const {
    if (const Tag* result = find(name)) return *result;
    throw std::runtime_error("NBT compound is missing tag: " + std::string(name));
}

std::int8_t Tag::as_byte() const {
    if (const auto* p = std::get_if<std::int8_t>(&value)) return *p;
    throw std::runtime_error("NBT tag is not a byte");
}
std::int16_t Tag::as_short() const {
    if (const auto* p = std::get_if<std::int16_t>(&value)) return *p;
    throw std::runtime_error("NBT tag is not a short");
}
std::int32_t Tag::as_int() const {
    if (const auto* p = std::get_if<std::int32_t>(&value)) return *p;
    throw std::runtime_error("NBT tag is not an int");
}
std::int64_t Tag::as_long() const {
    if (const auto* p = std::get_if<std::int64_t>(&value)) return *p;
    throw std::runtime_error("NBT tag is not a long");
}
float Tag::as_float() const {
    if (const auto* p = std::get_if<float>(&value)) return *p;
    throw std::runtime_error("NBT tag is not a float");
}
double Tag::as_double() const {
    if (const auto* p = std::get_if<double>(&value)) return *p;
    throw std::runtime_error("NBT tag is not a double");
}
const std::string& Tag::as_string() const {
    if (const auto* p = std::get_if<std::string>(&value)) return *p;
    throw std::runtime_error("NBT tag is not a string");
}
const Tag::ByteArray& Tag::as_byte_array() const {
    if (const auto* p = std::get_if<ByteArray>(&value)) return *p;
    throw std::runtime_error("NBT tag is not a byte array");
}
const Tag::IntArray& Tag::as_int_array() const {
    if (const auto* p = std::get_if<IntArray>(&value)) return *p;
    throw std::runtime_error("NBT tag is not an int array");
}
const Tag::LongArray& Tag::as_long_array() const {
    if (const auto* p = std::get_if<LongArray>(&value)) return *p;
    throw std::runtime_error("NBT tag is not a long array");
}

Document parse(std::span<const std::uint8_t> input, const Limits& limits) {
    if (limits.maximum_depth == 0 || limits.maximum_tags == 0 ||
        limits.maximum_array_elements == 0)
        throw std::runtime_error("NBT parser limits must be positive");
    return Reader(input, limits).read_document();
}

std::vector<std::uint8_t> decompress(
    std::span<const std::uint8_t> compressed, std::size_t maximum_output_bytes) {
    if (compressed.empty() || maximum_output_bytes == 0 ||
        compressed.size() > std::numeric_limits<uInt>::max() ||
        maximum_output_bytes > std::numeric_limits<uInt>::max())
        throw std::runtime_error("Invalid compressed NBT input or output limit");

    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(compressed.data()));
    stream.avail_in = static_cast<uInt>(compressed.size());
    if (inflateInit2(&stream, 15 + 32) != Z_OK)
        throw std::runtime_error("Could not initialize NBT decompressor");

    std::vector<std::uint8_t> output;
    output.reserve(std::min<std::size_t>(maximum_output_bytes, 1024U * 1024U));
    std::array<std::uint8_t, 64U * 1024U> buffer{};
    int status = Z_OK;
    while (status == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());
        status = inflate(&stream, Z_NO_FLUSH);
        const std::size_t produced = buffer.size() - stream.avail_out;
        if (produced > maximum_output_bytes - output.size()) {
            inflateEnd(&stream);
            throw std::runtime_error("Decompressed NBT exceeds the configured output limit");
        }
        output.insert(output.end(), buffer.begin(),
                      buffer.begin() + static_cast<std::ptrdiff_t>(produced));
    }
    const bool valid = status == Z_STREAM_END && stream.avail_in == 0;
    inflateEnd(&stream);
    if (!valid) throw std::runtime_error("Invalid or truncated gzip/zlib NBT stream");
    return output;
}

} // namespace minecpp::nbt
