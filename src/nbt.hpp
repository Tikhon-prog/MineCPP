#pragma once

// NBT (Named Binary Tag): Minecraft's tree format for chunks, level.dat and
// network payloads. Big-endian, no padding.
//
// - Strings are kept as the raw bytes of the file, which are Java "modified
//   UTF-8". Use mutf8_to_utf8 / utf8_to_mutf8 when you need real UTF-8.
// - Compound entries keep file order; a repeated key replaces the earlier value
//   (vanilla reads into a map, so the last one wins).
// - The reader is hardened for untrusted input: nesting depth is capped at 512
//   like vanilla, array and list lengths are checked against the bytes left
//   before anything is allocated, and truncated data throws.
// - Network NBT since 1.20.2 has a nameless root; pass `nameless = true`.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace minecpp::nbt {

enum class TagType : std::uint8_t {
    End = 0,
    Byte = 1,
    Short = 2,
    Int = 3,
    Long = 4,
    Float = 5,
    Double = 6,
    ByteArray = 7,
    String = 8,
    List = 9,
    Compound = 10,
    IntArray = 11,
    LongArray = 12,
};

constexpr int kMaxDepth = 512;

struct Tag;

struct List {
    TagType element = TagType::End;  // type of every item; End only when empty
    std::vector<Tag> items;
};

struct Compound {
    std::vector<std::pair<std::string, Tag>> entries;
};

struct Tag {
    // Alternative order matches TagType values 1..12.
    using Value = std::variant<std::int8_t, std::int16_t, std::int32_t, std::int64_t, float, double,
                               std::vector<std::int8_t>, std::string, List, Compound,
                               std::vector<std::int32_t>, std::vector<std::int64_t>>;
    Value value;

    TagType type() const { return static_cast<TagType>(value.index() + 1); }

    template <typename T, typename... Args>
    static Tag make(Args&&... args) {
        return Tag{Value(std::in_place_type<T>, std::forward<Args>(args)...)};
    }

    template <typename T>
    const T* get_if() const { return std::get_if<T>(&value); }
    template <typename T>
    T* get_if() { return std::get_if<T>(&value); }
    template <typename T>
    const T& as() const { return std::get<T>(value); }
    template <typename T>
    T& as() { return std::get<T>(value); }
};

inline const Tag* find(const Compound& c, std::string_view key) {
    for (const auto& entry : c.entries) {
        if (entry.first == key) {
            return &entry.second;
        }
    }
    return nullptr;
}

// Inserts, or replaces the value of an existing key (keeping its position).
inline void set(Compound& c, std::string key, Tag value) {
    for (auto& entry : c.entries) {
        if (entry.first == key) {
            entry.second = std::move(value);
            return;
        }
    }
    c.entries.emplace_back(std::move(key), std::move(value));
}

struct Root {
    std::string name;  // empty for a nameless root
    Tag tag;
};

// ---------------------------------------------------------------- reading

namespace detail {

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size) : p_(data), end_(data + size) {}

    std::size_t remaining() const { return static_cast<std::size_t>(end_ - p_); }

    std::uint8_t u8() {
        need(1);
        return *p_++;
    }
    std::uint16_t u16() {
        need(2);
        const auto v = static_cast<std::uint16_t>((p_[0] << 8) | p_[1]);
        p_ += 2;
        return v;
    }
    std::uint32_t u32() {
        need(4);
        const std::uint32_t v = (static_cast<std::uint32_t>(p_[0]) << 24) | (static_cast<std::uint32_t>(p_[1]) << 16) |
                                (static_cast<std::uint32_t>(p_[2]) << 8) | static_cast<std::uint32_t>(p_[3]);
        p_ += 4;
        return v;
    }
    std::uint64_t u64() {
        const std::uint64_t hi = u32();
        const std::uint64_t lo = u32();
        return (hi << 32) | lo;
    }
    const std::uint8_t* take(std::size_t n) {
        need(n);
        const std::uint8_t* start = p_;
        p_ += n;
        return start;
    }

private:
    void need(std::size_t n) const {
        if (remaining() < n) {
            throw std::runtime_error("NBT: unexpected end of data");
        }
    }

    const std::uint8_t* p_;
    const std::uint8_t* end_;
};

inline std::size_t checked_length(Reader& r, std::size_t element_bytes) {
    const auto len = static_cast<std::int32_t>(r.u32());
    if (len < 0) {
        throw std::runtime_error("NBT: negative array length");
    }
    const auto n = static_cast<std::size_t>(len);
    if (n > r.remaining() / element_bytes) {
        throw std::runtime_error("NBT: array length exceeds data");
    }
    return n;
}

inline Tag read_payload(Reader& r, TagType type, int depth);

inline std::string read_string(Reader& r) {
    const std::size_t len = r.u16();
    const std::uint8_t* bytes = r.take(len);
    return std::string(reinterpret_cast<const char*>(bytes), len);
}

inline Tag read_payload(Reader& r, TagType type, int depth) {
    switch (type) {
        case TagType::Byte:
            return Tag::make<std::int8_t>(static_cast<std::int8_t>(r.u8()));
        case TagType::Short:
            return Tag::make<std::int16_t>(static_cast<std::int16_t>(r.u16()));
        case TagType::Int:
            return Tag::make<std::int32_t>(static_cast<std::int32_t>(r.u32()));
        case TagType::Long:
            return Tag::make<std::int64_t>(static_cast<std::int64_t>(r.u64()));
        case TagType::Float: {
            const std::uint32_t bits = r.u32();
            float f;
            std::memcpy(&f, &bits, sizeof f);
            return Tag::make<float>(f);
        }
        case TagType::Double: {
            const std::uint64_t bits = r.u64();
            double d;
            std::memcpy(&d, &bits, sizeof d);
            return Tag::make<double>(d);
        }
        case TagType::ByteArray: {
            const std::size_t n = checked_length(r, 1);
            const std::uint8_t* bytes = r.take(n);
            std::vector<std::int8_t> out(n);
            if (n != 0) {
                std::memcpy(out.data(), bytes, n);
            }
            return Tag::make<std::vector<std::int8_t>>(std::move(out));
        }
        case TagType::String:
            return Tag::make<std::string>(read_string(r));
        case TagType::List: {
            if (depth > kMaxDepth) {
                throw std::runtime_error("NBT: nesting too deep");
            }
            const std::uint8_t raw = r.u8();
            if (raw > static_cast<std::uint8_t>(TagType::LongArray)) {
                throw std::runtime_error("NBT: unknown list element type");
            }
            const auto element = static_cast<TagType>(raw);
            const auto len = static_cast<std::int32_t>(r.u32());
            List list;
            list.element = element;
            if (len <= 0) {
                return Tag::make<List>(std::move(list));
            }
            if (element == TagType::End) {
                throw std::runtime_error("NBT: non-empty list of End");
            }
            if (static_cast<std::size_t>(len) > r.remaining()) {
                throw std::runtime_error("NBT: list length exceeds data");
            }
            list.items.reserve(static_cast<std::size_t>(len));
            for (std::int32_t i = 0; i < len; ++i) {
                list.items.push_back(read_payload(r, element, depth + 1));
            }
            return Tag::make<List>(std::move(list));
        }
        case TagType::Compound: {
            if (depth > kMaxDepth) {
                throw std::runtime_error("NBT: nesting too deep");
            }
            Compound c;
            for (;;) {
                const std::uint8_t raw = r.u8();
                if (raw == 0) {
                    break;
                }
                if (raw > static_cast<std::uint8_t>(TagType::LongArray)) {
                    throw std::runtime_error("NBT: unknown tag type");
                }
                std::string key = read_string(r);
                Tag value = read_payload(r, static_cast<TagType>(raw), depth + 1);
                set(c, std::move(key), std::move(value));
            }
            return Tag::make<Compound>(std::move(c));
        }
        case TagType::IntArray: {
            const std::size_t n = checked_length(r, 4);
            std::vector<std::int32_t> out;
            out.reserve(n);
            for (std::size_t i = 0; i < n; ++i) {
                out.push_back(static_cast<std::int32_t>(r.u32()));
            }
            return Tag::make<std::vector<std::int32_t>>(std::move(out));
        }
        case TagType::LongArray: {
            const std::size_t n = checked_length(r, 8);
            std::vector<std::int64_t> out;
            out.reserve(n);
            for (std::size_t i = 0; i < n; ++i) {
                out.push_back(static_cast<std::int64_t>(r.u64()));
            }
            return Tag::make<std::vector<std::int64_t>>(std::move(out));
        }
        case TagType::End:
            break;
    }
    throw std::runtime_error("NBT: invalid tag type");
}

}  // namespace detail

// Reads one root tag. A named root (files, chunks) must be a compound; a
// nameless root (network, 1.20.2+) may be any tag except End. Trailing bytes
// after the root are ignored; use read_root_bytes_used when that matters.
inline Root read_root(const std::uint8_t* data, std::size_t size, bool nameless = false) {
    detail::Reader r(data, size);
    const std::uint8_t raw = r.u8();
    if (raw == 0 || raw > static_cast<std::uint8_t>(TagType::LongArray)) {
        throw std::runtime_error("NBT: invalid root tag type");
    }
    const auto type = static_cast<TagType>(raw);
    Root root;
    if (!nameless) {
        if (type != TagType::Compound) {
            throw std::runtime_error("NBT: named root must be a compound");
        }
        root.name = detail::read_string(r);
    }
    root.tag = detail::read_payload(r, type, 0);
    return root;
}

inline Root read_root(const std::vector<std::uint8_t>& bytes, bool nameless = false) {
    return read_root(bytes.data(), bytes.size(), nameless);
}

// ---------------------------------------------------------------- writing

namespace detail {

class Writer {
public:
    std::vector<std::uint8_t> out;

    void u8(std::uint8_t v) { out.push_back(v); }
    void u16(std::uint16_t v) {
        out.push_back(static_cast<std::uint8_t>(v >> 8));
        out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    }
    void u32(std::uint32_t v) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            out.push_back(static_cast<std::uint8_t>((v >> shift) & 0xFF));
        }
    }
    void u64(std::uint64_t v) {
        for (int shift = 56; shift >= 0; shift -= 8) {
            out.push_back(static_cast<std::uint8_t>((v >> shift) & 0xFF));
        }
    }
    void string(const std::string& s) {
        if (s.size() > 0xFFFF) {
            throw std::invalid_argument("NBT: string longer than 65535 bytes");
        }
        u16(static_cast<std::uint16_t>(s.size()));
        out.insert(out.end(), s.begin(), s.end());
    }
};

inline void write_length(Writer& w, std::size_t n) {
    if (n > 0x7FFFFFFFU) {
        throw std::invalid_argument("NBT: array too long");
    }
    w.u32(static_cast<std::uint32_t>(n));
}

inline void write_payload(Writer& w, const Tag& tag) {
    switch (tag.type()) {
        case TagType::Byte:
            w.u8(static_cast<std::uint8_t>(tag.as<std::int8_t>()));
            return;
        case TagType::Short:
            w.u16(static_cast<std::uint16_t>(tag.as<std::int16_t>()));
            return;
        case TagType::Int:
            w.u32(static_cast<std::uint32_t>(tag.as<std::int32_t>()));
            return;
        case TagType::Long:
            w.u64(static_cast<std::uint64_t>(tag.as<std::int64_t>()));
            return;
        case TagType::Float: {
            const float f = tag.as<float>();
            std::uint32_t bits;
            std::memcpy(&bits, &f, sizeof bits);
            w.u32(bits);
            return;
        }
        case TagType::Double: {
            const double d = tag.as<double>();
            std::uint64_t bits;
            std::memcpy(&bits, &d, sizeof bits);
            w.u64(bits);
            return;
        }
        case TagType::ByteArray: {
            const auto& v = tag.as<std::vector<std::int8_t>>();
            write_length(w, v.size());
            for (const std::int8_t b : v) {
                w.u8(static_cast<std::uint8_t>(b));
            }
            return;
        }
        case TagType::String:
            w.string(tag.as<std::string>());
            return;
        case TagType::List: {
            const List& list = tag.as<List>();
            if (list.items.empty()) {
                w.u8(static_cast<std::uint8_t>(list.element));
                w.u32(0);
                return;
            }
            if (list.element == TagType::End) {
                throw std::invalid_argument("NBT: non-empty list of End");
            }
            for (const Tag& item : list.items) {
                if (item.type() != list.element) {
                    throw std::invalid_argument("NBT: list item type differs from element type");
                }
            }
            w.u8(static_cast<std::uint8_t>(list.element));
            write_length(w, list.items.size());
            for (const Tag& item : list.items) {
                write_payload(w, item);
            }
            return;
        }
        case TagType::Compound: {
            for (const auto& entry : tag.as<Compound>().entries) {
                w.u8(static_cast<std::uint8_t>(entry.second.type()));
                w.string(entry.first);
                write_payload(w, entry.second);
            }
            w.u8(0);
            return;
        }
        case TagType::IntArray: {
            const auto& v = tag.as<std::vector<std::int32_t>>();
            write_length(w, v.size());
            for (const std::int32_t x : v) {
                w.u32(static_cast<std::uint32_t>(x));
            }
            return;
        }
        case TagType::LongArray: {
            const auto& v = tag.as<std::vector<std::int64_t>>();
            write_length(w, v.size());
            for (const std::int64_t x : v) {
                w.u64(static_cast<std::uint64_t>(x));
            }
            return;
        }
        case TagType::End:
            break;
    }
    throw std::invalid_argument("NBT: cannot write End tag");
}

}  // namespace detail

inline std::vector<std::uint8_t> write_root(const std::string& name, const Tag& tag, bool nameless = false) {
    detail::Writer w;
    w.u8(static_cast<std::uint8_t>(tag.type()));
    if (!nameless) {
        w.string(name);
    }
    detail::write_payload(w, tag);
    return std::move(w.out);
}

// --------------------------------------------------- modified UTF-8 <-> UTF-8
//
// Java's format differs from UTF-8 in two ways: U+0000 is the two bytes C0 80,
// and code points above U+FFFF are written as a UTF-16 surrogate pair with each
// half encoded in three bytes (six bytes total).

namespace detail {

inline void append_utf8_3(std::string& out, std::uint32_t cp) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
}

}  // namespace detail

inline std::string mutf8_to_utf8(std::string_view in) {
    const std::size_t n = in.size();
    const auto u = [&](std::size_t k) { return static_cast<std::uint32_t>(static_cast<unsigned char>(in[k])); };
    const auto cont = [&](std::size_t k) { return k < n && (u(k) & 0xC0) == 0x80; };
    const auto bad = [] { return std::invalid_argument("invalid modified UTF-8"); };

    std::string out;
    out.reserve(n);
    std::size_t i = 0;
    while (i < n) {
        const std::uint32_t b = u(i);
        if (b < 0x80) {
            out.push_back(static_cast<char>(b));
            ++i;
        } else if (b == 0xC0 && cont(i + 1) && u(i + 1) == 0x80) {
            out.push_back('\0');
            i += 2;
        } else if (b >= 0xC2 && b <= 0xDF && cont(i + 1)) {
            out.append(in.substr(i, 2));
            i += 2;
        } else if ((b & 0xF0) == 0xE0 && cont(i + 1) && cont(i + 2)) {
            const std::uint32_t cp = ((b & 0x0F) << 12) | ((u(i + 1) & 0x3F) << 6) | (u(i + 2) & 0x3F);
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                if (i + 5 < n && u(i + 3) == 0xED && cont(i + 4) && cont(i + 5)) {
                    const std::uint32_t lo = ((u(i + 3) & 0x0F) << 12) | ((u(i + 4) & 0x3F) << 6) | (u(i + 5) & 0x3F);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        const std::uint32_t full = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        out.push_back(static_cast<char>(0xF0 | (full >> 18)));
                        out.push_back(static_cast<char>(0x80 | ((full >> 12) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | ((full >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (full & 0x3F)));
                        i += 6;
                        continue;
                    }
                }
                throw bad();  // unpaired high surrogate
            }
            if (cp >= 0xDC00 && cp <= 0xDFFF) {
                throw bad();  // unpaired low surrogate
            }
            out.append(in.substr(i, 3));
            i += 3;
        } else {
            throw bad();
        }
    }
    return out;
}

inline std::string utf8_to_mutf8(std::string_view in) {
    const std::size_t n = in.size();
    const auto u = [&](std::size_t k) { return static_cast<std::uint32_t>(static_cast<unsigned char>(in[k])); };
    const auto cont = [&](std::size_t k) { return k < n && (u(k) & 0xC0) == 0x80; };
    const auto bad = [] { return std::invalid_argument("invalid UTF-8"); };

    std::string out;
    out.reserve(n);
    std::size_t i = 0;
    while (i < n) {
        const std::uint32_t b = u(i);
        if (b == 0) {
            out.push_back(static_cast<char>(0xC0));
            out.push_back(static_cast<char>(0x80));
            ++i;
        } else if (b < 0x80) {
            out.push_back(static_cast<char>(b));
            ++i;
        } else if (b >= 0xC2 && b <= 0xDF && cont(i + 1)) {
            out.append(in.substr(i, 2));
            i += 2;
        } else if ((b & 0xF0) == 0xE0 && cont(i + 1) && cont(i + 2)) {
            out.append(in.substr(i, 3));
            i += 3;
        } else if (b >= 0xF0 && b <= 0xF4 && cont(i + 1) && cont(i + 2) && cont(i + 3)) {
            const std::uint32_t cp =
                ((b & 0x07) << 18) | ((u(i + 1) & 0x3F) << 12) | ((u(i + 2) & 0x3F) << 6) | (u(i + 3) & 0x3F);
            if (cp < 0x10000 || cp > 0x10FFFF) {
                throw bad();
            }
            const std::uint32_t v = cp - 0x10000;
            detail::append_utf8_3(out, 0xD800 + (v >> 10));
            detail::append_utf8_3(out, 0xDC00 + (v & 0x3FF));
            i += 4;
        } else {
            throw bad();
        }
    }
    return out;
}

}  // namespace minecpp::nbt
