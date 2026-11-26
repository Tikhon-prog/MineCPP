#include "items.hpp"

#include <iostream>
#include <stdexcept>

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

int main() {
    const ItemRegistry reg = ItemRegistry::with_basics();

    // Registry basics.
    {
        CHECK(reg.get(kAirItem).name == "minecraft:air");
        const auto stone = reg.find("minecraft:stone");
        CHECK(stone.has_value() && *stone == 1U);
        CHECK(reg.get(*stone).max_stack == 64);
        CHECK(reg.get(*reg.find("minecraft:ender_pearl")).max_stack == 16);
        const ItemDef& sword = reg.get(*reg.find("minecraft:diamond_sword"));
        CHECK(sword.max_stack == 1 && sword.max_damage == 1561);
        CHECK(!reg.find("minecraft:nope").has_value());
        CHECK(throws([&] { (void)reg.get(9999); }));
    }

    // Registration rules.
    {
        ItemRegistry r;
        const ItemId a = r.register_item("test:a");
        CHECK(a == 1U);
        CHECK(throws([&] { r.register_item("test:a"); }));
        CHECK(throws([&] { r.register_item("test:b", 0); }));
        CHECK(throws([&] { r.register_item("test:c", 100); }));
        CHECK(throws([&] { r.register_item("test:d", 16, 100); }));  // durable must be 1
    }

    const ItemId stone = *reg.find("minecraft:stone");
    const ItemId dirt = *reg.find("minecraft:dirt");
    const ItemId pearl = *reg.find("minecraft:ender_pearl");
    const ItemId sword = *reg.find("minecraft:diamond_sword");

    // move_into: fill an empty stack, top up, overflow, mismatch.
    {
        ItemStack dst;
        ItemStack src{stone, 100};
        CHECK(move_into(dst, src, reg) == 64);
        CHECK(dst.item == stone && dst.count == 64);
        CHECK(src.count == 36);
        CHECK(move_into(dst, src, reg) == 0);  // already full

        ItemStack part{stone, 60};
        CHECK(move_into(part, src, reg) == 4);
        CHECK(part.count == 64 && src.count == 32);

        ItemStack other{dirt, 1};
        CHECK(move_into(other, src, reg) == 0);
        CHECK(src.count == 32);

        ItemStack tail{stone, 10};
        ItemStack last{stone, 5};
        CHECK(move_into(tail, last, reg) == 5);
        CHECK(last.is_empty());  // normalised to an empty stack
        CHECK(tail.count == 15);
    }

    // split.
    {
        ItemStack s{stone, 10};
        const ItemStack half = split(s, 4);
        CHECK(half.count == 4 && s.count == 6);
        const ItemStack rest = split(s, 100);
        CHECK(rest.count == 6 && s.is_empty());
        CHECK(split(s, 3).is_empty());
    }

    // Inventory add: stacks up first, then spills to empty slots.
    {
        Inventory inv;
        CHECK(inv.add({stone, 70}, reg).is_empty());
        CHECK(inv.slot(0).count == 64 && inv.slot(1).count == 6);
        CHECK(inv.add({stone, 10}, reg).is_empty());
        CHECK(inv.slot(1).count == 16);
        CHECK(inv.count(stone) == 80);

        CHECK(inv.add({sword, 1}, reg).is_empty());
        CHECK(inv.add({sword, 1}, reg).is_empty());
        CHECK(inv.slot(2).item == sword && inv.slot(3).item == sword);  // never merge
        CHECK(inv.first_empty() == 4);

        CHECK(inv.add({pearl, 20}, reg).is_empty());
        CHECK(inv.slot(4).count == 16 && inv.slot(5).count == 4);
    }

    // A full inventory returns the leftover.
    {
        Inventory inv;
        for (int i = 0; i < Inventory::kSlots; ++i) {
            inv.set_slot(i, {dirt, 64});
        }
        CHECK(inv.first_empty() == -1);
        const ItemStack left = inv.add({stone, 5}, reg);
        CHECK(left.item == stone && left.count == 5);
        const ItemStack left2 = inv.add({dirt, 3}, reg);
        CHECK(left2.count == 3);
    }

    // remove takes from the last slots first.
    {
        Inventory inv;
        inv.set_slot(0, {stone, 10});
        inv.set_slot(20, {stone, 10});
        CHECK(inv.remove(stone, 15) == 15);
        CHECK(inv.slot(20).is_empty());
        CHECK(inv.slot(0).count == 5);
        CHECK(inv.remove(stone, 100) == 5);
        CHECK(inv.count(stone) == 0);
    }

    // Slot bounds; empty stacks normalise to air.
    {
        Inventory inv;
        CHECK(throws([&] { (void)inv.slot(36); }));
        CHECK(throws([&] { inv.set_slot(-1, {stone, 1}); }));
        inv.set_slot(3, {stone, 0});
        CHECK(inv.slot(3).item == kAirItem);
    }

    std::cout << "items tests passed\n";
    return 0;
}
