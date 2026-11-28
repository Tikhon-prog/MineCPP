#include "json.hpp"

#include <cstdlib>
#include <stdexcept>

namespace minecpp {

const Json& Json::null_value() {
    static const Json value;
    return value;
}

const Json* Json::find(std::string_view key) const {
    if (type != Type::Object) return nullptr;
    for (std::size_t i = keys.size(); i-- > 0;) {
        if (keys[i] == key) return &items[i];
    }
    return nullptr;
}

const Json& Json::operator[](std::string_view key) const {
    const Json* found = find(key);
    return found != nullptr ? *found : null_value();
}

const Json& Json::operator[](std::size_t index) const {
    if (type != Type::Array || index >= items.size()) return null_value();
    return items[index];
}

namespace {

constexpr int kMaxDepth = 64;

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    Json parse() {
        skip_space();
        Json value = parse_value(0);
        skip_space();
        if (pos_ != text_.size()) fail("unexpected data after the value");
        return value;
    }

private:
    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error("JSON: " + message + " at offset " + std::to_string(pos_));
    }

    bool at_end() const { return pos_ >= text_.size(); }
    char peek() const { return at_end() ? '\0' : text_[pos_]; }

    void skip_space() {
        while (!at_end() && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' || text_[pos_] == '\r')) {
            ++pos_;
        }
    }

    void expect(char c) {
        if (peek() != c) fail(std::string("expected '") + c + "'");
        ++pos_;
    }

    bool consume_word(std::string_view word) {
        if (text_.substr(pos_, word.size()) == word) {
            pos_ += word.size();
            return true;
        }
        return false;
    }

    Json parse_value(int depth) {
        if (depth > kMaxDepth) fail("nesting too deep");
        const char c = peek();
        Json value;
        if (c == '{') {
            parse_object(value, depth);
        } else if (c == '[') {
            parse_array(value, depth);
        } else if (c == '"') {
            value.type = Json::Type::String;
            value.string = parse_string();
        } else if (consume_word("true")) {
            value.type = Json::Type::Bool;
            value.boolean = true;
        } else if (consume_word("false")) {
            value.type = Json::Type::Bool;
        } else if (consume_word("null")) {
            // stays null
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            value.type = Json::Type::Number;
            value.number = parse_number();
        } else {
            fail("unexpected character");
        }
        return value;
    }

    void parse_object(Json& out, int depth) {
        out.type = Json::Type::Object;
        expect('{');
        skip_space();
        if (peek() == '}') {
            ++pos_;
            return;
        }
        for (;;) {
            skip_space();
            if (peek() != '"') fail("expected a string key");
            std::string key = parse_string();
            skip_space();
            expect(':');
            skip_space();
            Json value = parse_value(depth + 1);
            out.keys.push_back(std::move(key));
            out.items.push_back(std::move(value));
            skip_space();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            expect('}');
            return;
        }
    }

    void parse_array(Json& out, int depth) {
        out.type = Json::Type::Array;
        expect('[');
        skip_space();
        if (peek() == ']') {
            ++pos_;
            return;
        }
        for (;;) {
            skip_space();
            out.items.push_back(parse_value(depth + 1));
            skip_space();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            expect(']');
            return;
        }
    }

    unsigned parse_hex4() {
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = peek();
            unsigned digit = 0;
            if (c >= '0' && c <= '9') digit = static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') digit = static_cast<unsigned>(c - 'a') + 10U;
            else if (c >= 'A' && c <= 'F') digit = static_cast<unsigned>(c - 'A') + 10U;
            else fail("bad \\u escape");
            value = value * 16U + digit;
            ++pos_;
        }
        return value;
    }

    static void append_utf8(std::string& out, unsigned cp) {
        if (cp < 0x80U) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800U) {
            out.push_back(static_cast<char>(0xC0U | (cp >> 6U)));
            out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
        } else if (cp < 0x10000U) {
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

    std::string parse_string() {
        expect('"');
        std::string out;
        for (;;) {
            if (at_end()) fail("unterminated string");
            const char c = text_[pos_++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20U) fail("control character in string");
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (at_end()) fail("unterminated escape");
            const char e = text_[pos_++];
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
                    unsigned cp = parse_hex4();
                    if (cp >= 0xD800U && cp <= 0xDBFFU) {
                        if (!consume_word("\\u")) fail("lone high surrogate");
                        const unsigned low = parse_hex4();
                        if (low < 0xDC00U || low > 0xDFFFU) fail("bad low surrogate");
                        cp = 0x10000U + ((cp - 0xD800U) << 10U) + (low - 0xDC00U);
                    } else if (cp >= 0xDC00U && cp <= 0xDFFFU) {
                        fail("lone low surrogate");
                    }
                    append_utf8(out, cp);
                    break;
                }
                default: fail("bad escape");
            }
        }
    }

    double parse_number() {
        const std::size_t start = pos_;
        if (peek() == '-') ++pos_;
        if (peek() == '0') {
            ++pos_;
        } else if (peek() >= '1' && peek() <= '9') {
            while (peek() >= '0' && peek() <= '9') ++pos_;
        } else {
            fail("bad number");
        }
        if (peek() == '.') {
            ++pos_;
            if (!(peek() >= '0' && peek() <= '9')) fail("bad number");
            while (peek() >= '0' && peek() <= '9') ++pos_;
        }
        if (peek() == 'e' || peek() == 'E') {
            ++pos_;
            if (peek() == '+' || peek() == '-') ++pos_;
            if (!(peek() >= '0' && peek() <= '9')) fail("bad number");
            while (peek() >= '0' && peek() <= '9') ++pos_;
        }
        const std::string token(text_.substr(start, pos_ - start));
        return std::strtod(token.c_str(), nullptr);
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

}  // namespace

Json parse_json(std::string_view text) {
    return Parser(text).parse();
}

}  // namespace minecpp
