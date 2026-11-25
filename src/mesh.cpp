#include "mesh.hpp"

#include <array>
#include <cstdlib>
#include <vector>

namespace minecpp {
namespace {

void push_vertex(std::vector<MeshVertex>& output, float x, float y, float z, Color3 color) {
    output.push_back({x, y, z, color.r, color.g, color.b});
}

float face_shade(int axis, bool positive) {
    if (axis == 0) return positive ? 0.78F : 0.66F;
    if (axis == 1) return positive ? 1.00F : 0.52F;
    return positive ? 0.72F : 0.86F;
}

} // namespace

std::vector<MeshVertex> build_chunk_mesh(const ChunkSnapshot& snapshot) {
    std::vector<MeshVertex> output;
    output.reserve(6000);
    const std::array<int, 3> dimensions{kChunkSize, kWorldHeight, kChunkSize};
    const float base_x = static_cast<float>(snapshot.position.x * kChunkSize);
    const float base_z = static_cast<float>(snapshot.position.z * kChunkSize);

    for (int axis = 0; axis < 3; ++axis) {
        const int u_axis = (axis + 1) % 3;
        const int v_axis = (axis + 2) % 3;
        const int u_size = dimensions[static_cast<std::size_t>(u_axis)];
        const int v_size = dimensions[static_cast<std::size_t>(v_axis)];
        std::vector<int> mask(static_cast<std::size_t>(u_size * v_size), 0);

        for (int slice = 0; slice <= dimensions[static_cast<std::size_t>(axis)]; ++slice) {
            for (int v = 0; v < v_size; ++v) {
                for (int u = 0; u < u_size; ++u) {
                    std::array<int, 3> left{};
                    std::array<int, 3> right{};
                    left[static_cast<std::size_t>(axis)] = slice - 1;
                    right[static_cast<std::size_t>(axis)] = slice;
                    left[static_cast<std::size_t>(u_axis)] = u;
                    right[static_cast<std::size_t>(u_axis)] = u;
                    left[static_cast<std::size_t>(v_axis)] = v;
                    right[static_cast<std::size_t>(v_axis)] = v;

                    const Block a = snapshot.at(left[0], left[1], left[2]);
                    const Block b = snapshot.at(right[0], right[1], right[2]);
                    int face = 0;
                    if (is_solid(a) && !is_solid(b)) face = static_cast<int>(a);
                    else if (!is_solid(a) && is_solid(b)) face = -static_cast<int>(b);
                    mask[static_cast<std::size_t>(v * u_size + u)] = face;
                }
            }

            for (int v = 0; v < v_size; ++v) {
                for (int u = 0; u < u_size;) {
                    const int face = mask[static_cast<std::size_t>(v * u_size + u)];
                    if (face == 0) {
                        ++u;
                        continue;
                    }

                    int width = 1;
                    while (u + width < u_size &&
                           mask[static_cast<std::size_t>(v * u_size + u + width)] == face)
                        ++width;

                    int height = 1;
                    bool can_grow = true;
                    while (v + height < v_size && can_grow) {
                        for (int du = 0; du < width; ++du) {
                            if (mask[static_cast<std::size_t>((v + height) * u_size + u + du)] != face) {
                                can_grow = false;
                                break;
                            }
                        }
                        if (can_grow) ++height;
                    }

                    const bool positive = face > 0;
                    const Color3 color = block_color(static_cast<Block>(std::abs(face)),
                                                     face_shade(axis, positive));
                    std::array<int, 3> origin{};
                    origin[static_cast<std::size_t>(axis)] = slice;
                    origin[static_cast<std::size_t>(u_axis)] = u;
                    origin[static_cast<std::size_t>(v_axis)] = v;

                    std::array<std::array<float, 3>, 4> corners{};
                    for (std::size_t i = 0; i < corners.size(); ++i) {
                        std::array<int, 3> point = origin;
                        if (i == 1 || i == 2) point[static_cast<std::size_t>(u_axis)] += width;
                        if (i == 2 || i == 3) point[static_cast<std::size_t>(v_axis)] += height;
                        corners[i] = {
                            static_cast<float>(point[0]) + base_x,
                            static_cast<float>(point[1]),
                            static_cast<float>(point[2]) + base_z
                        };
                    }

                    constexpr std::array<int, 6> positive_indices{0, 1, 2, 0, 2, 3};
                    constexpr std::array<int, 6> negative_indices{0, 2, 1, 0, 3, 2};
                    const auto& indices = positive ? positive_indices : negative_indices;
                    for (const int index : indices) {
                        const auto& vertex = corners[static_cast<std::size_t>(index)];
                        push_vertex(output, vertex[0], vertex[1], vertex[2], color);
                    }

                    for (int dv = 0; dv < height; ++dv)
                        for (int du = 0; du < width; ++du)
                            mask[static_cast<std::size_t>((v + dv) * u_size + u + du)] = 0;
                    u += width;
                }
            }
        }
    }
    return output;
}

} // namespace minecpp
