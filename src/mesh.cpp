#include "mesh.hpp"

#include <array>

namespace minecpp {
namespace {
struct Face {
    int dx, dy, dz;
    float shade;
    std::array<std::array<float, 3>, 4> corners;
};

constexpr std::array<Face, 6> kFaces{{
    { 0, 1, 0, 1.00F, {{{0,1,0},{0,1,1},{1,1,1},{1,1,0}}}},
    { 0,-1, 0, 0.52F, {{{0,0,0},{1,0,0},{1,0,1},{0,0,1}}}},
    { 1, 0, 0, 0.78F, {{{1,0,0},{1,1,0},{1,1,1},{1,0,1}}}},
    {-1, 0, 0, 0.66F, {{{0,0,0},{0,0,1},{0,1,1},{0,1,0}}}},
    { 0, 0, 1, 0.72F, {{{0,0,1},{1,0,1},{1,1,1},{0,1,1}}}},
    { 0, 0,-1, 0.86F, {{{0,0,0},{0,1,0},{1,1,0},{1,0,0}}}}
}};

void push_vertex(std::vector<MeshVertex>& out, float x, float y, float z, Color3 color) {
    out.push_back({x, y, z, color.r, color.g, color.b});
}
}

std::vector<MeshVertex> build_chunk_mesh(const ChunkSnapshot& snapshot) {
    std::vector<MeshVertex> result;
    result.reserve(12000);
    const float base_x = static_cast<float>(snapshot.position.x * kChunkSize);
    const float base_z = static_cast<float>(snapshot.position.z * kChunkSize);

    for (int y = 0; y < kWorldHeight; ++y) {
        for (int z = 0; z < kChunkSize; ++z) {
            for (int x = 0; x < kChunkSize; ++x) {
                const Block block = snapshot.at(x, y, z);
                if (block == Block::Air) continue;
                for (const Face& face : kFaces) {
                    if (snapshot.at(x + face.dx, y + face.dy, z + face.dz) != Block::Air) continue;
                    const Color3 color = block_color(block, face.shade);
                    constexpr std::array<int, 6> indices{0, 1, 2, 0, 2, 3};
                    for (int index : indices) {
                        const auto& corner = face.corners[static_cast<std::size_t>(index)];
                        push_vertex(result, base_x + static_cast<float>(x) + corner[0],
                                    static_cast<float>(y) + corner[1],
                                    base_z + static_cast<float>(z) + corner[2], color);
                    }
                }
            }
        }
    }
    return result;
}

} // namespace minecpp
