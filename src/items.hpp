#pragma once

// Item registry, stacks and a player inventory.
//
// Stacks compare by item id only. Vanilla stacks also carry data components
// (damage, enchantments, names); two stacks only merge when those match too.
// Components are a later step, so merging here is correct only for plain items.

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace minecpp {

using ItemId = std::uint32_t;
constexpr ItemId kAirItem = 0;

struct ItemDef {
    std::string name;     // namespaced, e.g. "minecraft:stone"
    int max_stack = 64;   // 1..99
    int max_damage = 0;   // 0 = no durability
};

class ItemRegistry {
public:
    ItemRegistry() { register_item("minecraft:air"); }

    // Ids are handed out in registration order starting at 1 (0 is air).
    ItemId register_item(const std::string& name, int max_stack = 64, int max_damage = 0) {
        if (by_name_.count(name) != 0) {
            throw std::invalid_argument("ItemRegistry: duplicate item " + name);
        }
        if (max_stack < 1 || max_stack > 99) {
            throw std::invalid_argument("ItemRegistry: max_stack must be 1..99");
        }
        if (max_damage < 0 || (max_damage > 0 && max_stack != 1)) {
            throw std::invalid_argument("ItemRegistry: durable items must stack to 1");
        }
        const auto id = static_cast<ItemId>(defs_.size());
        defs_.push_back({name, max_stack, max_damage});
        by_name_.emplace(name, id);
        return id;
    }

    const ItemDef& get(ItemId id) const {
        if (id >= defs_.size()) {
            throw std::out_of_range("ItemRegistry: unknown item id");
        }
        return defs_[id];
    }

    std::optional<ItemId> find(const std::string& name) const {
        const auto it = by_name_.find(name);
        if (it == by_name_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::size_t size() const noexcept { return defs_.size(); }

    // A small starter set with vanilla stack sizes.
    static ItemRegistry with_basics() {
        ItemRegistry r;
        r.register_item("minecraft:stone");
        r.register_item("minecraft:dirt");
        r.register_item("minecraft:oak_log");
        r.register_item("minecraft:stick");
        r.register_item("minecraft:apple");
        r.register_item("minecraft:ender_pearl", 16);
        r.register_item("minecraft:snowball", 16);
        r.register_item("minecraft:egg", 16);
        r.register_item("minecraft:iron_pickaxe", 1, 250);
        r.register_item("minecraft:diamond_sword", 1, 1561);
        return r;
    }

private:
    std::vector<ItemDef> defs_;
    std::unordered_map<std::string, ItemId> by_name_;
};

struct ItemStack {
    ItemId item = kAirItem;
    int count = 0;

    bool is_empty() const noexcept { return item == kAirItem || count <= 0; }
};

// Moves as many items as fit from `src` into `dst`. Returns how many moved.
// `dst` must be empty or hold the same item; otherwise nothing moves.
inline int move_into(ItemStack& dst, ItemStack& src, const ItemRegistry& registry) {
    if (src.is_empty()) {
        return 0;
    }
    const int max = registry.get(src.item).max_stack;
    if (dst.is_empty()) {
        const int n = std::min(src.count, max);
        dst = {src.item, n};
        src.count -= n;
        if (src.count == 0) {
            src = {};
        }
        return n;
    }
    if (dst.item != src.item) {
        return 0;
    }
    const int room = max - dst.count;
    const int n = std::min(room, src.count);
    if (n <= 0) {
        return 0;
    }
    dst.count += n;
    src.count -= n;
    if (src.count == 0) {
        src = {};
    }
    return n;
}

// Takes up to `n` items off the stack and returns them as a new stack.
inline ItemStack split(ItemStack& stack, int n) {
    if (stack.is_empty() || n <= 0) {
        return {};
    }
    const int taken = std::min(n, stack.count);
    ItemStack out{stack.item, taken};
    stack.count -= taken;
    if (stack.count == 0) {
        stack = {};
    }
    return out;
}

// 36 main slots: 0-8 hotbar, 9-35 storage.
class Inventory {
public:
    static constexpr int kSlots = 36;
    static constexpr int kHotbar = 9;

    const ItemStack& slot(int i) const {
        check(i);
        return slots_[static_cast<std::size_t>(i)];
    }
    void set_slot(int i, ItemStack s) {
        check(i);
        slots_[static_cast<std::size_t>(i)] = s.is_empty() ? ItemStack{} : s;
    }

    // Adds a stack like picking up an item: top up existing stacks of the same
    // item first, then fill the first empty slots. Returns what did not fit.
    ItemStack add(ItemStack stack, const ItemRegistry& registry) {
        if (stack.is_empty()) {
            return {};
        }
        for (ItemStack& s : slots_) {
            if (!s.is_empty() && s.item == stack.item) {
                move_into(s, stack, registry);
                if (stack.is_empty()) {
                    return {};
                }
            }
        }
        for (ItemStack& s : slots_) {
            if (s.is_empty()) {
                move_into(s, stack, registry);
                if (stack.is_empty()) {
                    return {};
                }
            }
        }
        return stack;
    }

    int count(ItemId item) const {
        int total = 0;
        for (const ItemStack& s : slots_) {
            if (!s.is_empty() && s.item == item) {
                total += s.count;
            }
        }
        return total;
    }

    // Removes up to `n` of `item`, last slots first. Returns how many were removed.
    int remove(ItemId item, int n) {
        int removed = 0;
        for (int i = kSlots - 1; i >= 0 && removed < n; --i) {
            ItemStack& s = slots_[static_cast<std::size_t>(i)];
            if (!s.is_empty() && s.item == item) {
                const int take = std::min(n - removed, s.count);
                s.count -= take;
                removed += take;
                if (s.count == 0) {
                    s = {};
                }
            }
        }
        return removed;
    }

    // First slot holding nothing, or -1.
    int first_empty() const {
        for (int i = 0; i < kSlots; ++i) {
            if (slots_[static_cast<std::size_t>(i)].is_empty()) {
                return i;
            }
        }
        return -1;
    }

private:
    static void check(int i) {
        if (i < 0 || i >= kSlots) {
            throw std::out_of_range("Inventory: slot out of range");
        }
    }

    std::array<ItemStack, kSlots> slots_{};
};

}  // namespace minecpp
