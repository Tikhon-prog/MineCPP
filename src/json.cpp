#include "json.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace minecpp::json {
namespace {

class Parser {
public:
    Parser(std::string_view input, std::size_t maximum_depth)
        : input_(input), maximum_depth_(maximum_depth) {}

    Value run() {
        skip_space();
        Value result = parse_value(0);
        skip_space();
        if (position_ != input_.size()) fail("unexpected trailing characters");
        return result;
    }

private:
    [[noreturn]] void fail(const char* message) const {
        throw std::runtime_error(
            std::string("Invalid JSON at byte ") + std::to_string(position_) + ": " + message);
    }

    void skip_space() {
        while (position_ < input_.size()) {
            const char c = input_[position_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++position_;
            else break;
        }
    }

    bool consume(char expected) {
        if (position_ < input_.size() && input_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    void expect(char expected) {
        if (!consume(expected)) fail("unexpected character");
    }

    Value parse_value(std::size_t depth) {
        if (depth > maximum_depth_) fail("maximum nesting depth exceeded");
        skip_space();
        if (position_ >= input_.size()) fail("unexpected end of input");

        switch (input_[position_]) {
            case 'n': parse_literal("null"); return Value{};
            case 't': parse_literal("true"); return Value(Value::Storage{true});
            case 'f': parse_literal("false"); return Value(Value::Storage{false});
            case '"': return Value(Value::Storage{parse_string()});
            case '[': return Value(Value::Storage{parse_array(depth + 1)});
            case '{': return Value(Value::Storage{parse_object(depth + 1)});
            default:
                if (input_[position_] == '-' ||
                    (input_[position_] >= '0' && input_[position_] <= '9'))
                    return Value(Value::Storage{parse_number()});
                fail("expected a JSON value");
        }
    }

    void parse_literal(std::string_view literal) {
        if (input_.substr(position_, literal.size()) != literal) fail("invalid literal");
        position_ += literal.size();
    }

    static void append_utf8(std::string& output, std::uint32_t codepoint) {
        if (codepoint <= 0x7fU) {
            output.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7ffU) {
            output.push_back(static_cast<char>(0xc0U | (codepoint >> 6U)));
            output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
        } else if (codepoint <= 0xffffU) {
            output.push_back(static_cast<char>(0xe0U | (codepoint >> 12U)));
            output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
        } else {
            output.push_back(static_cast<char>(0xf0U | (codepoint >> 18U)));
            output.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
        }
    }

    std::uint32_t parse_hex_quad() {
        if (input_.size() - position_ < 4) fail("incomplete Unicode escape");
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = input_[position_++];
            value <<= 4U;
            if (c >= '0' && c <= '9') value |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<std::uint32_t>(c - 'A' + 10);
            else fail("invalid hexadecimal digit in Unicode escape");
        }
        return value;
    }

    std::string parse_string() {
        expect('"');
        std::string output;
        while (position_ < input_.size()) {
            const unsigned char c = static_cast<unsigned char>(input_[position_++]);
            if (c == '"') return output;
            if (c < 0x20U) fail("unescaped control character in string");
            if (c != '\\') {
                if (c < 0x80U) {
                    output.push_back(static_cast<char>(c));
                    continue;
                }
                unsigned int continuation_count = 0;
                std::uint32_t codepoint = 0;
                std::uint32_t minimum = 0;
                if (c >= 0xc2U && c <= 0xdfU) {
                    continuation_count = 1;
                    codepoint = c & 0x1fU;
                    minimum = 0x80U;
                } else if (c >= 0xe0U && c <= 0xefU) {
                    continuation_count = 2;
                    codepoint = c & 0x0fU;
                    minimum = 0x800U;
                } else if (c >= 0xf0U && c <= 0xf4U) {
                    continuation_count = 3;
                    codepoint = c & 0x07U;
                    minimum = 0x10000U;
                } else {
                    fail("invalid UTF-8 leading byte");
                }
                if (input_.size() - position_ < continuation_count)
                    fail("incomplete UTF-8 sequence");
                output.push_back(static_cast<char>(c));
                for (unsigned int i = 0; i < continuation_count; ++i) {
                    const unsigned char next =
                        static_cast<unsigned char>(input_[position_++]);
                    if ((next & 0xc0U) != 0x80U)
                        fail("invalid UTF-8 continuation byte");
                    codepoint = (codepoint << 6U) | (next & 0x3fU);
                    output.push_back(static_cast<char>(next));
                }
                if (codepoint < minimum || codepoint > 0x10ffffU ||
                    (codepoint >= 0xd800U && codepoint <= 0xdfffU))
                    fail("invalid UTF-8 code point");
                continue;
            }
            if (position_ >= input_.size()) fail("incomplete escape sequence");
            const char escaped = input_[position_++];
            switch (escaped) {
                case '"': output.push_back('"'); break;
                case '\\': output.push_back('\\'); break;
                case '/': output.push_back('/'); break;
                case 'b': output.push_back('\b'); break;
                case 'f': output.push_back('\f'); break;
                case 'n': output.push_back('\n'); break;
                case 'r': output.push_back('\r'); break;
                case 't': output.push_back('\t'); break;
                case 'u': {
                    std::uint32_t codepoint = parse_hex_quad();
                    if (codepoint >= 0xd800U && codepoint <= 0xdbffU) {
                        if (input_.size() - position_ < 6 ||
                            input_[position_] != '\\' || input_[position_ + 1] != 'u')
                            fail("high surrogate without a low surrogate");
                        position_ += 2;
                        const std::uint32_t low = parse_hex_quad();
                        if (low < 0xdc00U || low > 0xdfffU) fail("invalid low surrogate");
                        codepoint = 0x10000U + ((codepoint - 0xd800U) << 10U) + (low - 0xdc00U);
                    } else if (codepoint >= 0xdc00U && codepoint <= 0xdfffU) {
                        fail("unpaired low surrogate");
                    }
                    append_utf8(output, codepoint);
                    break;
                }
                default: fail("unknown escape sequence");
            }
        }
        fail("unterminated string");
    }

    double parse_number() {
        const std::size_t begin = position_;
        consume('-');
        if (consume('0')) {
            if (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9')
                fail("leading zero in number");
        } else {
            if (position_ >= input_.size() || input_[position_] < '1' || input_[position_] > '9')
                fail("invalid integer part");
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9')
                ++position_;
        }
        if (consume('.')) {
            const std::size_t fraction_start = position_;
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9')
                ++position_;
            if (fraction_start == position_) fail("fraction requires at least one digit");
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-'))
                ++position_;
            const std::size_t exponent_start = position_;
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9')
                ++position_;
            if (exponent_start == position_) fail("exponent requires at least one digit");
        }

        double value = 0.0;
        const auto token = input_.substr(begin, position_ - begin);
        const auto conversion = std::from_chars(
            token.data(), token.data() + token.size(), value, std::chars_format::general);
        if (conversion.ec != std::errc{} || conversion.ptr != token.data() + token.size() ||
            !std::isfinite(value))
            fail("number is outside the supported range");
        return value;
    }

    Value::Array parse_array(std::size_t depth) {
        expect('[');
        skip_space();
        Value::Array result;
        if (consume(']')) return result;
        for (;;) {
            result.push_back(parse_value(depth));
            skip_space();
            if (consume(']')) return result;
            expect(',');
            skip_space();
            if (position_ < input_.size() && input_[position_] == ']')
                fail("trailing comma in array");
        }
    }

    Value::Object parse_object(std::size_t depth) {
        expect('{');
        skip_space();
        Value::Object result;
        if (consume('}')) return result;
        for (;;) {
            if (position_ >= input_.size() || input_[position_] != '"')
                fail("object key must be a string");
            std::string key = parse_string();
            skip_space();
            expect(':');
            Value value = parse_value(depth);
            if (!result.emplace(std::move(key), std::move(value)).second)
                fail("duplicate object key");
            skip_space();
            if (consume('}')) return result;
            expect(',');
            skip_space();
            if (position_ < input_.size() && input_[position_] == '}')
                fail("trailing comma in object");
        }
    }

    std::string_view input_;
    std::size_t maximum_depth_;
    std::size_t position_ = 0;
};

} // namespace

bool Value::is_null() const noexcept { return std::holds_alternative<std::nullptr_t>(value_); }
bool Value::is_bool() const noexcept { return std::holds_alternative<bool>(value_); }
bool Value::is_number() const noexcept { return std::holds_alternative<double>(value_); }
bool Value::is_string() const noexcept { return std::holds_alternative<std::string>(value_); }
bool Value::is_array() const noexcept { return std::holds_alternative<Array>(value_); }
bool Value::is_object() const noexcept { return std::holds_alternative<Object>(value_); }

bool Value::as_bool() const {
    if (const auto* value = std::get_if<bool>(&value_)) return *value;
    throw std::runtime_error("JSON value is not a boolean");
}
double Value::as_number() const {
    if (const auto* value = std::get_if<double>(&value_)) return *value;
    throw std::runtime_error("JSON value is not a number");
}
const std::string& Value::as_string() const {
    if (const auto* value = std::get_if<std::string>(&value_)) return *value;
    throw std::runtime_error("JSON value is not a string");
}
const Value::Array& Value::as_array() const {
    if (const auto* value = std::get_if<Array>(&value_)) return *value;
    throw std::runtime_error("JSON value is not an array");
}
const Value::Object& Value::as_object() const {
    if (const auto* value = std::get_if<Object>(&value_)) return *value;
    throw std::runtime_error("JSON value is not an object");
}
const Value* Value::find(std::string_view key) const noexcept {
    const auto* object = std::get_if<Object>(&value_);
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

Value parse(std::string_view input, std::size_t maximum_depth) {
    return Parser(input, maximum_depth).run();
}

} // namespace minecpp::json
