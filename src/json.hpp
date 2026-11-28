#pragma once

// Minimal JSON reader for Minecraft version files. Strict RFC 8259 syntax,
// nesting limited to 64 levels, UTF-8 output for \u escapes (surrogate pairs
// included). Objects keep their key order; when a key repeats, find() returns
// the last one, like most parsers.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace minecpp {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Json> items;         // array elements, or object values
    std::vector<std::string> keys;   // object keys, parallel to `items`

    bool is_null() const { return type == Type::Null; }
    bool is_bool() const { return type == Type::Bool; }
    bool is_number() const { return type == Type::Number; }
    bool is_string() const { return type == Type::String; }
    bool is_array() const { return type == Type::Array; }
    bool is_object() const { return type == Type::Object; }

    std::size_t size() const { return items.size(); }

    // Object member or nullptr.
    const Json* find(std::string_view key) const;
    // Object member, or a shared null value when absent (never throws).
    const Json& operator[](std::string_view key) const;
    // Array element, or a shared null value when out of range.
    const Json& operator[](std::size_t index) const;

    std::string as_string(const std::string& fallback = {}) const { return is_string() ? string : fallback; }
    int as_int(int fallback = 0) const { return is_number() ? static_cast<int>(number) : fallback; }
    bool as_bool(bool fallback = false) const { return is_bool() ? boolean : fallback; }

    static const Json& null_value();
};

// Throws std::runtime_error with a position on malformed input.
Json parse_json(std::string_view text);

}  // namespace minecpp
