#include "json.hpp"

#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <string_view>

namespace minecpp {

bool Json::boolean(bool fallback) const {
    return is_bool() ? std::get<bool>(value_) : fallback;
}

double Json::number(double fallback) const {
    return is_number() ? std::get<double>(value_) : fallback;
}

std::string Json::string(std::string fallback) const {
    return is_string() ? std::get<std::string>(value_) : std::move(fallback);
}

const Json::Array& Json::array() const {
    static const Array empty;
    return is_array() ? std::get<Array>(value_) : empty;
}

const Json::Object& Json::object() const {
    static const Object empty;
    return is_object() ? std::get<Object>(value_) : empty;
}

const Json* Json::get(std::string_view key) const {
    if (!is_object()) return nullptr;
    const auto& obj = std::get<Object>(value_);
    const auto it = obj.find(key);
    return it == obj.end() ? nullptr : &it->second;
}

std::string Json::get_string(std::string_view key, std::string fallback) const {
    const Json* value = get(key);
    return value ? value->string(std::move(fallback)) : std::move(fallback);
}

bool Json::contains(std::string_view key) const { return get(key) != nullptr; }

namespace {
class Parser {
public:
    explicit Parser(const std::string& input) : input_(input) {}

    Json parse() {
        skip_ws();
        Json result = parse_value();
        skip_ws();
        if (pos_ != input_.size()) fail("unexpected characters after JSON value");
        return result;
    }

private:
    const std::string& input_;
    std::size_t pos_ = 0;

    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error("JSON parse error at byte " + std::to_string(pos_) + ": " + message);
    }

    void skip_ws() {
        while (pos_ < input_.size() && (input_[pos_] == ' ' || input_[pos_] == '\n' || input_[pos_] == '\r' || input_[pos_] == '\t')) ++pos_;
    }

    bool consume(char c) {
        if (pos_ < input_.size() && input_[pos_] == c) { ++pos_; return true; }
        return false;
    }

    void expect(char c) { if (!consume(c)) fail(std::string("expected '") + c + "'"); }

    static void append_utf8(std::string& out, std::uint32_t cp) {
        if (cp <= 0x7FU) out.push_back(static_cast<char>(cp));
        else if (cp <= 0x7FFU) {
            out.push_back(static_cast<char>(0xC0U | (cp >> 6U)));
            out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
        } else if (cp <= 0xFFFFU) {
            out.push_back(static_cast<char>(0xE0U | (cp >> 12U)));
            out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
        } else {
            out.push_back(static_cast<char>(0xF0U | (cp >> 18U)));
            out.push_back(static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
        }
    }

    std::uint32_t hex4() {
        if (pos_ + 4 > input_.size()) fail("incomplete unicode escape");
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = input_[pos_++];
            value <<= 4U;
            if (c >= '0' && c <= '9') value |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<std::uint32_t>(c - 'A' + 10);
            else fail("invalid unicode escape");
        }
        return value;
    }

    std::string parse_string() {
        expect('"');
        std::string out;
        while (pos_ < input_.size()) {
            const unsigned char c = static_cast<unsigned char>(input_[pos_++]);
            if (c == '"') return out;
            if (c < 0x20U) fail("control character in string");
            if (c != '\\') { out.push_back(static_cast<char>(c)); continue; }
            if (pos_ >= input_.size()) fail("incomplete escape");
            const char e = input_[pos_++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    std::uint32_t cp = hex4();
                    if (cp >= 0xD800U && cp <= 0xDBFFU) {
                        if (pos_ + 6 > input_.size() || input_[pos_] != '\\' || input_[pos_ + 1] != 'u')
                            fail("high surrogate without low surrogate");
                        pos_ += 2;
                        const std::uint32_t low = hex4();
                        if (low < 0xDC00U || low > 0xDFFFU) fail("invalid low surrogate");
                        cp = 0x10000U + ((cp - 0xD800U) << 10U) + (low - 0xDC00U);
                    } else if (cp >= 0xDC00U && cp <= 0xDFFFU) {
                        fail("unexpected low surrogate");
                    }
                    append_utf8(out, cp);
                    break;
                }
                default: fail("invalid string escape");
            }
        }
        fail("unterminated string");
    }

    Json parse_number() {
        const std::size_t start = pos_;
        consume('-');
        if (consume('0')) { }
        else {
            if (pos_ >= input_.size() || input_[pos_] < '1' || input_[pos_] > '9') fail("invalid number");
            while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') ++pos_;
        }
        if (consume('.')) {
            if (pos_ >= input_.size() || input_[pos_] < '0' || input_[pos_] > '9') fail("invalid fraction");
            while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') ++pos_;
        }
        if (pos_ < input_.size() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-')) ++pos_;
            if (pos_ >= input_.size() || input_[pos_] < '0' || input_[pos_] > '9') fail("invalid exponent");
            while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') ++pos_;
        }
        const std::string token = input_.substr(start, pos_ - start);
        char* end = nullptr;
        const double value = std::strtod(token.c_str(), &end);
        if (!end || *end != '\0' || !std::isfinite(value)) fail("number out of range");
        return Json(Json::Value(value));
    }

    Json parse_array() {
        expect('['); skip_ws(); Json::Array values;
        if (consume(']')) return Json(Json::Value(std::move(values)));
        while (true) {
            skip_ws(); values.push_back(parse_value()); skip_ws();
            if (consume(']')) break;
            expect(',');
        }
        return Json(Json::Value(std::move(values)));
    }

    Json parse_object() {
        expect('{'); skip_ws(); Json::Object values;
        if (consume('}')) return Json(Json::Value(std::move(values)));
        while (true) {
            skip_ws();
            if (pos_ >= input_.size() || input_[pos_] != '"') fail("object key must be a string");
            std::string key = parse_string(); skip_ws(); expect(':'); skip_ws();
            values.insert_or_assign(std::move(key), parse_value()); skip_ws();
            if (consume('}')) break;
            expect(',');
        }
        return Json(Json::Value(std::move(values)));
    }

    Json parse_value() {
        if (pos_ >= input_.size()) fail("expected value");
        switch (input_[pos_]) {
            case '{': return parse_object();
            case '[': return parse_array();
            case '"': return Json(Json::Value(parse_string()));
            case 't': if (input_.compare(pos_, 4, "true") == 0) { pos_ += 4; return Json(Json::Value(true)); } break;
            case 'f': if (input_.compare(pos_, 5, "false") == 0) { pos_ += 5; return Json(Json::Value(false)); } break;
            case 'n': if (input_.compare(pos_, 4, "null") == 0) { pos_ += 4; return Json(); } break;
            default:
                if (input_[pos_] == '-' || (input_[pos_] >= '0' && input_[pos_] <= '9')) return parse_number();
        }
        fail("invalid value");
    }
};
} // namespace

Json Json::parse(const std::string& text) { return Parser(text).parse(); }

} // namespace minecpp
