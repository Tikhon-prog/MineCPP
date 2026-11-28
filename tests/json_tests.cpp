#include "json.hpp"
#include "md5.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

using namespace minecpp;

#define CHECK(expression) do { if (!(expression)) throw std::runtime_error("Test failed: " #expression); } while (false)

template <typename Fn>
static bool throws(Fn&& fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

static std::string hex(const std::array<std::uint8_t, 16>& digest) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    for (const std::uint8_t b : digest) {
        out.push_back(kHex[b >> 4U]);
        out.push_back(kHex[b & 0x0FU]);
    }
    return out;
}

int main() {
    // Scalars.
    {
        CHECK(parse_json("null").is_null());
        CHECK(parse_json(" true ").as_bool());
        CHECK(!parse_json("false").as_bool(true));
        CHECK(parse_json("42").as_int() == 42);
        CHECK(parse_json("-7").as_int() == -7);
        CHECK(parse_json("3.5").number == 3.5);
        CHECK(parse_json("1e3").number == 1000.0);
        CHECK(parse_json("-0.25E-1").number == -0.025);
        CHECK(parse_json("\"hi\"").as_string() == "hi");
    }

    // Strings and escapes.
    {
        CHECK(parse_json("\"a\\\"b\\\\c\\/d\\n\\t\"").string == "a\"b\\c/d\n\t");
        CHECK(parse_json("\"\\u0041\\u00e9\"").string == "A\xC3\xA9");          // A, e-acute
        CHECK(parse_json("\"\\u20ac\"").string == "\xE2\x82\xAC");               // euro sign
        CHECK(parse_json("\"\\ud83d\\ude00\"").string == "\xF0\x9F\x98\x80");   // surrogate pair
        CHECK(parse_json("\"raw \xC3\xA9\"").string == "raw \xC3\xA9");          // UTF-8 passes through
    }

    // Containers and lookup helpers.
    {
        const Json j = parse_json(R"({"a": 1, "b": [true, null, "x", {"c": 2}], "a": 3, "e": {}, "f": []})");
        CHECK(j.is_object());
        CHECK(j["a"].as_int() == 3);                 // last duplicate wins
        CHECK(j["b"].size() == 4);
        CHECK(j["b"][0].as_bool());
        CHECK(j["b"][1].is_null());
        CHECK(j["b"][2].as_string() == "x");
        CHECK(j["b"][3]["c"].as_int() == 2);
        CHECK(j["e"].is_object() && j["e"].size() == 0);
        CHECK(j["f"].is_array() && j["f"].size() == 0);
        CHECK(j.find("nope") == nullptr);
        CHECK(j["nope"]["deeper"]["still"].is_null());   // missing chains are null, not errors
        CHECK(j["b"][99].is_null());
        CHECK(j["a"].as_string("fallback") == "fallback");  // wrong type -> fallback
        CHECK(j["nope"].as_int(5) == 5);
        CHECK(j.keys.size() == 5);  // key order kept, duplicates kept
        CHECK(j.keys[0] == "a" && j.keys[1] == "b");
    }

    // Malformed input throws instead of guessing.
    {
        for (const char* bad : {"", "{", "[1,", "{\"a\"}", "{\"a\":}", "[1 2]", "tru", "01", "1.", "+1", "\"abc",
                                "\"\\x\"", "\"\\u12\"", "\"\\ud800\"", "\"\\udc00\"", "{'a':1}", "[1,]", "{\"a\":1,}",
                                "1 2", "\"tab\there\""}) {
            CHECK(throws([&] { (void)parse_json(bad); }));
        }
        // Deep nesting is refused rather than overflowing the stack.
        CHECK(throws([] { (void)parse_json(std::string(70, '[') + std::string(70, ']')); }));
        CHECK(!throws([] { (void)parse_json(std::string(60, '[') + std::string(60, ']')); }));
    }

    // MD5 test vectors and Minecraft's offline UUIDs.
    {
        CHECK(hex(md5("")) == "d41d8cd98f00b204e9800998ecf8427e");
        CHECK(hex(md5("abc")) == "900150983cd24fb0d6963f7d28e17f72");
        CHECK(hex(md5("The quick brown fox jumps over the lazy dog")) == "9e107d9d372bb6826bd81d3542a419d6");
        // Longer than one block, to cross the padding boundary.
        CHECK(hex(md5(std::string(1000, 'a'))) == "cabe45dcc9ae5b66ba86600cca6b8ba8");
        CHECK(offline_player_uuid("Notch") == "b50ad385829d3141a2167e7d7539ba7f");
        CHECK(offline_player_uuid("Player") == "a01e3843e5213998958af459800e4d11");
        CHECK(offline_player_uuid("Steve_1") == "ed378328805a3b9a940a29bd82fbcf20");
    }

    std::cout << "json tests passed\n";
    return 0;
}
