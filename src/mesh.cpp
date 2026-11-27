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

// Written once by set_mesh_uv_table before any chunk is meshed.
TileUvTable& uv_table() {
    static TileUvTable table;
    return table;
}

// Texture coordinate (0..1 inside the tile) of one face corner. Side faces put
// the top of the texture at the top of the block, seen from outside.
std::array<float, 2> face_uv(std::size_t face, const std::array<float, 3>& corner) {
    const float cx = corner[0];
    const float cy = corner[1];
    const float cz = corner[2];
    switch (face) {
        case 0: return {cx, cz};
        case 1: return {cx, 1.0F - cz};
        case 2: return {1.0F - cz, 1.0F - cy};
        case 3: return {cz, 1.0F - cy};
        case 4: return {cx, 1.0F - cy};
        default: return {1.0F - cx, 1.0F - cy};
    }
}

void push_vertex(std::vector<MeshVertex>& out, float x, float y, float z, Color3 color, float u, float v) {
    out.push_back({x, y, z, color.r, color.g, color.b, u, v});
}
}

void set_mesh_uv_table(const TileUvTable& table) {
    uv_table() = table;
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
                for (std::size_t face_index = 0; face_index < kFaces.size(); ++face_index) {
                    const Face& face = kFaces[face_index];
                    if (snapshot.at(x + face.dx, y + face.dy, z + face.dz) != Block::Air) continue;
                    const TileUvTable& uvs = uv_table();
                    const FaceKind kind = face_index == 0 ? FaceKind::Top
                                        : (face_index == 1 ? FaceKind::Bottom : FaceKind::Side);
                    // With an atlas the texture supplies the colour and the vertex
                    // colour only carries the directional shade.
                    const Color3 color = uvs.enabled ? Color3{face.shade, face.shade, face.shade}
                                                     : block_color(block, face.shade);
                    const UvRect rect = uvs.uv[static_cast<std::size_t>(tile_for(block, kind))];
                    constexpr std::array<int, 6> indices{0, 1, 2, 0, 2, 3};
                    for (int index : indices) {
                        const auto& corner = face.corners[static_cast<std::size_t>(index)];
                        const auto uv = face_uv(face_index, corner);
                        push_vertex(result, base_x + static_cast<float>(x) + corner[0],
                                    static_cast<float>(y) + corner[1],
                                    base_z + static_cast<float>(z) + corner[2], color,
                                    rect.u0 + uv[0] * (rect.u1 - rect.u0),
                                    rect.v0 + uv[1] * (rect.v1 - rect.v0));
                    }
                }
            }
        }
    }
    return result;
}

} // namespace minecpp
