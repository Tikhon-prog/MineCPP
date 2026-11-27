#include "nbt.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace minecpp::nbt;

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

using Bytes = std::vector<std::uint8_t>;

static Bytes bytes_of(const std::string& s) { return Bytes(s.begin(), s.end()); }

int main() {
    // The canonical "hello world" sample from the NBT spec.
    {
        const Bytes sample = {0x0A, 0x00, 0x0B, 'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd',
                              0x08, 0x00, 0x04, 'n', 'a', 'm', 'e', 0x00, 0x09,
                              'B', 'a', 'n', 'a', 'n', 'r', 'a', 'm', 'a', 0x00};
        const Root root = read_root(sample);
        CHECK(root.name == "hello world");
        CHECK(root.tag.type() == TagType::Compound);
        const Compound& c = root.tag.as<Compound>();
        const Tag* name = find(c, "name");
        CHECK(name != nullptr);
        CHECK(name->as<std::string>() == "Bananrama");
        CHECK(find(c, "missing") == nullptr);
        CHECK(write_root(root.name, root.tag) == sample);
    }

    // Every tag type survives a write/read round trip.
    {
        Compound c;
        set(c, "byte", Tag::make<std::int8_t>(-5));
        set(c, "short", Tag::make<std::int16_t>(-12345));
        set(c, "int", Tag::make<std::int32_t>(-1234567890));
        set(c, "long", Tag::make<std::int64_t>(-1234567890123456789LL));
        set(c, "float", Tag::make<float>(3.5F));
        set(c, "double", Tag::make<double>(-0.125));
        set(c, "bytes", Tag::make<std::vector<std::int8_t>>(std::vector<std::int8_t>{1, -2, 3}));
        set(c, "string", Tag::make<std::string>("abc"));
        List ints;
        ints.element = TagType::Int;
        ints.items.push_back(Tag::make<std::int32_t>(7));
        ints.items.push_back(Tag::make<std::int32_t>(8));
        set(c, "list", Tag::make<List>(ints));
        Compound inner;
        set(inner, "x", Tag::make<std::int8_t>(1));
        set(c, "compound", Tag::make<Compound>(inner));
        set(c, "ints", Tag::make<std::vector<std::int32_t>>(std::vector<std::int32_t>{1, -1, 0x7FFFFFFF}));
        set(c, "longs", Tag::make<std::vector<std::int64_t>>(std::vector<std::int64_t>{1, -1, INT64_MIN}));

        const Tag original = Tag::make<Compound>(c);
        const Bytes encoded = write_root("root", original);
        const Root back = read_root(encoded);
        CHECK(back.name == "root");
        const Compound& bc = back.tag.as<Compound>();
        CHECK(bc.entries.size() == c.entries.size());
        CHECK(find(bc, "byte")->as<std::int8_t>() == -5);
        CHECK(find(bc, "short")->as<std::int16_t>() == -12345);
        CHECK(find(bc, "int")->as<std::int32_t>() == -1234567890);
        CHECK(find(bc, "long")->as<std::int64_t>() == -1234567890123456789LL);
        CHECK(find(bc, "float")->as<float>() == 3.5F);
        CHECK(find(bc, "double")->as<double>() == -0.125);
        CHECK((find(bc, "bytes")->as<std::vector<std::int8_t>>() == std::vector<std::int8_t>{1, -2, 3}));
        CHECK(find(bc, "string")->as<std::string>() == "abc");
        const List& l = find(bc, "list")->as<List>();
        CHECK(l.element == TagType::Int && l.items.size() == 2 && l.items[1].as<std::int32_t>() == 8);
        CHECK(find(find(bc, "compound")->as<Compound>(), "x")->as<std::int8_t>() == 1);
        CHECK((find(bc, "ints")->as<std::vector<std::int32_t>>() == std::vector<std::int32_t>{1, -1, 0x7FFFFFFF}));
        CHECK((find(bc, "longs")->as<std::vector<std::int64_t>>() == std::vector<std::int64_t>{1, -1, INT64_MIN}));
        // Re-encoding gives the same bytes (entry order is preserved).
        CHECK(write_root("root", back.tag) == encoded);
    }

    // Big-endian layout of an int.
    {
        const Bytes b = write_root("", Tag::make<std::int32_t>(0x01020304), true);
        const Bytes expected = {0x03, 0x01, 0x02, 0x03, 0x04};
        CHECK(b == expected);
        const Root r = read_root(b, true);
        CHECK(r.tag.as<std::int32_t>() == 0x01020304);
        CHECK(r.name.empty());
    }

    // Nameless root can be a non-compound; a named root cannot.
    {
        const Bytes b = {0x08, 0x00, 0x02, 'h', 'i'};
        CHECK(read_root(b, true).tag.as<std::string>() == "hi");
        CHECK(throws([&] { read_root(b, false); }));
    }

    // Duplicate keys: the last value wins, in the first key's position.
    {
        const Bytes b = {0x0A, 0x00, 0x00,
                         0x01, 0x00, 0x01, 'a', 0x01,
                         0x01, 0x00, 0x01, 'b', 0x02,
                         0x01, 0x00, 0x01, 'a', 0x09,
                         0x00};
        const Root root = read_root(b);  // keep the Root alive: `c` points into it
        const Compound& c = root.tag.as<Compound>();
        CHECK(c.entries.size() == 2);
        CHECK(c.entries[0].first == "a" && c.entries[0].second.as<std::int8_t>() == 9);
        CHECK(c.entries[1].first == "b");
    }

    // Empty list keeps its element type; negative length reads as empty.
    {
        const Bytes b = {0x09, 0x03, 0x00, 0x00, 0x00, 0x00};
        const Root r = read_root(b, true);
        CHECK(r.tag.as<List>().element == TagType::Int && r.tag.as<List>().items.empty());
        CHECK(write_root("", r.tag, true) == b);
        const Bytes neg = {0x09, 0x03, 0xFF, 0xFF, 0xFF, 0xFF};
        CHECK(read_root(neg, true).tag.as<List>().items.empty());
    }

    // Float and double special values keep their bits.
    {
        const Root r = read_root(write_root("", Tag::make<float>(INFINITY), true), true);
        CHECK(std::isinf(r.tag.as<float>()));
        const Root d = read_root(write_root("", Tag::make<double>(NAN), true), true);
        CHECK(std::isnan(d.tag.as<double>()));
    }

    // Hostile input.
    {
        // Truncated everywhere.
        const Bytes full = write_root("r", Tag::make<Compound>(Compound{}));
        for (std::size_t cut = 0; cut + 1 < full.size(); ++cut) {
            CHECK(throws([&] { read_root(full.data(), cut); }));
        }
        // Byte array claiming 2 GiB.
        const Bytes huge = {0x07, 0x7F, 0xFF, 0xFF, 0xFF, 0x00};
        CHECK(throws([&] { read_root(huge, true); }));
        // Int array and long array length overflow.
        const Bytes huge_ints = {0x0B, 0x7F, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00};
        CHECK(throws([&] { read_root(huge_ints, true); }));
        const Bytes neg_array = {0x0C, 0xFF, 0xFF, 0xFF, 0xFF};
        CHECK(throws([&] { read_root(neg_array, true); }));
        // List claiming a million items with nothing behind it.
        const Bytes huge_list = {0x09, 0x01, 0x00, 0x0F, 0x42, 0x40};
        CHECK(throws([&] { read_root(huge_list, true); }));
        // Non-empty list of End.
        const Bytes end_list = {0x09, 0x00, 0x00, 0x00, 0x00, 0x01};
        CHECK(throws([&] { read_root(end_list, true); }));
        // Unknown tag types.
        CHECK(throws([&] { read_root(Bytes{0x0D, 0x00}, true); }));
        CHECK(throws([&] { read_root(Bytes{0x00}, true); }));
        // Deep nesting: lists of lists 600 levels down.
        Bytes deep = {0x09};
        for (int i = 0; i < 600; ++i) {
            deep.insert(deep.end(), {0x09, 0x00, 0x00, 0x00, 0x01});
        }
        deep.insert(deep.end(), {0x00, 0x00, 0x00, 0x00, 0x00});
        CHECK(throws([&] { read_root(deep, true); }));
    }

    // Writer refuses malformed trees.
    {
        List mixed;
        mixed.element = TagType::Int;
        mixed.items.push_back(Tag::make<std::int8_t>(1));
        CHECK(throws([&] { write_root("", Tag::make<List>(mixed), true); }));
        List end_items;
        end_items.element = TagType::End;
        end_items.items.push_back(Tag::make<std::int8_t>(1));
        CHECK(throws([&] { write_root("", Tag::make<List>(end_items), true); }));
        CHECK(throws([&] { write_root(std::string(70000, 'x'), Tag::make<Compound>(Compound{})); }));
    }

    // Modified UTF-8.
    {
        CHECK(utf8_to_mutf8(std::string("a\0b", 3)) == std::string("a\xC0\x80" "b", 4));
        CHECK(mutf8_to_utf8(std::string("a\xC0\x80" "b", 4)) == std::string("a\0b", 3));

        const std::string emoji = "\xF0\x9F\x98\x80";  // U+1F600
        const std::string java = "\xED\xA0\xBD\xED\xB8\x80";
        CHECK(utf8_to_mutf8(emoji) == java);
        CHECK(mutf8_to_utf8(java) == emoji);

        const std::string mixed = "h\xC3\xA9llo \xE2\x82\xAC \xF0\x9F\x98\x80!";  // h, e-acute, euro, emoji
        CHECK(mutf8_to_utf8(utf8_to_mutf8(mixed)) == mixed);

        CHECK(throws([] { (void)mutf8_to_utf8("\xED\xA0\xBD"); }));          // lone high surrogate
        CHECK(throws([] { (void)mutf8_to_utf8("\xED\xB8\x80"); }));          // lone low surrogate
        CHECK(throws([] { (void)mutf8_to_utf8("\xF0\x9F\x98\x80"); }));      // 4-byte form is not modified UTF-8
        CHECK(throws([] { (void)mutf8_to_utf8("\xC3"); }));                  // truncated
        CHECK(throws([] { (void)utf8_to_mutf8("\xFF"); }));
    }

    (void)bytes_of;
    std::cout << "nbt tests passed\n";
    return 0;
}
