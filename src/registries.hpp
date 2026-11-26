#pragma once

// Interning registries that turn names found in saved worlds into the small
// integer ids used by ChunkSection.
//
// BlockStateRegistry: a block state is a name plus string properties, for
// example minecraft:oak_log[axis=y]. Properties are sorted by key so the same
// state always gets the same id. Id 0 is minecraft:air.
// NameRegistry: plain interned strings, used for biomes.
//
// Ids depend on the order states were first seen, so they are valid for one
// loaded world only. Never store them; save names.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace minecpp {

struct BlockState {
    std::string name;
    std::vector<std::pair<std::string, std::string>> properties;
};

class BlockStateRegistry {
public:
    using Id = std::uint32_t;

    BlockStateRegistry() { intern(BlockState{"minecraft:air", {}}); }

    Id intern(BlockState state) {
        std::sort(state.properties.begin(), state.properties.end());
        const std::string key = key_of(state);
        const auto it = ids_.find(key);
        if (it != ids_.end()) {
            return it->second;
        }
        const auto id = static_cast<Id>(states_.size());
        states_.push_back(std::move(state));
        ids_.emplace(key, id);
        return id;
    }

    const BlockState& get(Id id) const {
        if (id >= states_.size()) {
            throw std::out_of_range("BlockStateRegistry: unknown id");
        }
        return states_[id];
    }

    std::size_t size() const noexcept { return states_.size(); }

    // "minecraft:oak_log[axis=y]", or just the name when there are no properties.
    static std::string key_of(const BlockState& state) {
        std::string key = state.name;
        if (!state.properties.empty()) {
            key += '[';
            for (std::size_t i = 0; i < state.properties.size(); ++i) {
                if (i != 0) {
                    key += ',';
                }
                key += state.properties[i].first;
                key += '=';
                key += state.properties[i].second;
            }
            key += ']';
        }
        return key;
    }

private:
    std::vector<BlockState> states_;
    std::unordered_map<std::string, Id> ids_;
};

class NameRegistry {
public:
    using Id = std::uint32_t;

    Id intern(const std::string& name) {
        const auto it = ids_.find(name);
        if (it != ids_.end()) {
            return it->second;
        }
        const auto id = static_cast<Id>(names_.size());
        names_.push_back(name);
        ids_.emplace(name, id);
        return id;
    }

    const std::string& get(Id id) const {
        if (id >= names_.size()) {
            throw std::out_of_range("NameRegistry: unknown id");
        }
        return names_[id];
    }

    std::size_t size() const noexcept { return names_.size(); }

private:
    std::vector<std::string> names_;
    std::unordered_map<std::string, Id> ids_;
};

}  // namespace minecpp
