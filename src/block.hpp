#pragma once

#include <cstdint>
#include <string_view>

namespace minecpp {

enum class Block : std::uint8_t {
    Air = 0,
    Grass,
    Dirt,
    Stone,
    Sand,
    Wood,
    Leaves,
    Bedrock
};

inline bool is_solid(Block block) { return block != Block::Air; }

inline std::string_view block_name(Block block) {
    switch (block) {
        case Block::Air: return "Air";
        case Block::Grass: return "Grass";
        case Block::Dirt: return "Dirt";
        case Block::Stone: return "Stone";
        case Block::Sand: return "Sand";
        case Block::Wood: return "Wood";
        case Block::Leaves: return "Leaves";
        case Block::Bedrock: return "Bedrock";
    }
    return "Unknown";
}

struct Color3 {
    float r;
    float g;
    float b;
};

inline Color3 block_color(Block block, float face_shade = 1.0F) {
    Color3 color{1.0F, 0.0F, 1.0F};
    switch (block) {
        case Block::Grass: color = {0.32F, 0.68F, 0.22F}; break;
        case Block::Dirt: color = {0.48F, 0.30F, 0.16F}; break;
        case Block::Stone: color = {0.48F, 0.50F, 0.53F}; break;
        case Block::Sand: color = {0.78F, 0.70F, 0.45F}; break;
        case Block::Wood: color = {0.43F, 0.27F, 0.12F}; break;
        case Block::Leaves: color = {0.16F, 0.49F, 0.18F}; break;
        case Block::Bedrock: color = {0.19F, 0.20F, 0.22F}; break;
        case Block::Air: color = {0.0F, 0.0F, 0.0F}; break;
    }
    return {color.r * face_shade, color.g * face_shade, color.b * face_shade};
}

} // namespace minecpp
