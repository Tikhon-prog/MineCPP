#include "block.hpp"
#include "chunk.hpp"
#include "math.hpp"
#include "mesh.hpp"
#include "world.hpp"

#include <stdexcept>
#include <cstddef>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

using namespace minecpp;

#define CHECK(expression) do { if (!(expression)) throw std::runtime_error("Test failed: " #expression); } while (false)

template<typename Function>
void expect_throw(Function&& function) {
    bool threw = false;
    try { function(); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
}

int main() {
    CHECK(floor_div(0, 16) == 0);
    CHECK(floor_div(15, 16) == 0);
    CHECK(floor_div(16, 16) == 1);
    CHECK(floor_div(-1, 16) == -1);
    CHECK(floor_div(-16, 16) == -1);
    CHECK(floor_div(-17, 16) == -2);
    CHECK(positive_mod(-1, 16) == 15);

    for (int z = -400; z <= 400; z += 37) {
        for (int x = -400; x <= 400; x += 41) {
            const int height = World::terrain_height(1337U, x, z);
            CHECK(height >= 7 && height <= 82);
            CHECK(height == World::terrain_height(1337U, x, z));
        }
    }

    const auto generated_a = World::generate_chunk_blocks({-2, 3}, 12345U);
    const auto generated_b = World::generate_chunk_blocks({-2, 3}, 12345U);
    CHECK(generated_a.size() == static_cast<std::size_t>(kChunkSize * kWorldHeight * kChunkSize));
    CHECK(generated_a == generated_b);

    for (int z = 0; z < kChunkSize; ++z) {
        for (int x = 0; x < kChunkSize; ++x) {
            CHECK(generated_a[block_index(x, 0, z)] == Block::Bedrock);
            const int world_x = -2 * kChunkSize + x;
            const int world_z = 3 * kChunkSize + z;
            const int top = World::terrain_height(12345U, world_x, world_z);
            const Block surface = generated_a[block_index(x, top, z)];
            CHECK(surface == Block::Grass || surface == Block::Sand);
            CHECK(generated_a[block_index(x, top + 1, z)] == Block::Air ||
                   generated_a[block_index(x, top + 1, z)] == Block::Wood ||
                   generated_a[block_index(x, top + 1, z)] == Block::Leaves);
        }
    }

    const BlockEdit edit{{-31, 20, 49}, Block::Wood};
    const auto edited = World::generate_chunk_blocks({-2, 3}, 12345U, {edit});
    CHECK(edited[block_index(1, 20, 1)] == Block::Wood);
    CHECK(edited != generated_a);

    ChunkSnapshot one;
    one.position = {0, 0};
    one.blocks.assign(static_cast<std::size_t>(kWorldHeight * (kChunkSize + 2) * (kChunkSize + 2)), Block::Air);
    const auto snapshot_index = [](int x, int y, int z) {
        return (static_cast<std::size_t>(y) * (kChunkSize + 2) + static_cast<std::size_t>(z + 1)) *
               (kChunkSize + 2) + static_cast<std::size_t>(x + 1);
    };
    one.blocks[snapshot_index(4, 10, 4)] = Block::Stone;
    CHECK(build_chunk_mesh(one).size() == 36U);

    ChunkSnapshot merged;
    merged.position = {0, 0};
    merged.blocks.assign(static_cast<std::size_t>(kWorldHeight * (kChunkSize + 2) * (kChunkSize + 2)), Block::Air);
    merged.blocks[snapshot_index(4, 10, 4)] = Block::Stone;
    merged.blocks[snapshot_index(5, 10, 4)] = Block::Stone;
    CHECK(build_chunk_mesh(merged).size() == 36U);

    one.blocks[snapshot_index(5, 10, 4)] = Block::Dirt;
    CHECK(build_chunk_mesh(one).size() == 60U);

    const auto save_path = std::filesystem::temp_directory_path() / "minecpp-world-tests.mcw";
    std::error_code remove_error;
    std::filesystem::remove(save_path, remove_error);
    {
        World world(24680U, 2);
        bool ready = false;
        for (int attempt = 0; attempt < 10000 && !ready; ++attempt) {
            world.update_streaming(8, 8);
            world.pump_jobs();
            ready = world.has_spawn_chunk();
            if (!ready) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK(ready);
        const int ground = World::terrain_height(world.seed(), 1, 1);
        CHECK(world.set_block(1, ground + 1, 1, Block::Wood));
        world.save(save_path);
    }
    {
        World restored(1U, 2);
        restored.load(save_path);
        CHECK(restored.seed() == 24680U);
        bool ready = false;
        for (int attempt = 0; attempt < 10000 && !ready; ++attempt) {
            restored.update_streaming(8, 8);
            restored.pump_jobs();
            ready = restored.has_spawn_chunk();
            if (!ready) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK(ready);
        const int ground = World::terrain_height(restored.seed(), 1, 1);
        CHECK(restored.get_block(1, ground + 1, 1) == Block::Wood);
        restored.save(save_path);
        CHECK(std::filesystem::is_regular_file(save_path));

        const auto corrupt_path = std::filesystem::path(save_path.string() + ".trailing");
        std::filesystem::copy_file(save_path, corrupt_path,
                                   std::filesystem::copy_options::overwrite_existing);
        {
            std::ofstream corrupt(corrupt_path, std::ios::binary | std::ios::app);
            corrupt.put('!');
        }
        World untouched(777U, 2);
        expect_throw([&] { untouched.load(corrupt_path); });
        CHECK(untouched.seed() == 777U);
        std::filesystem::remove(corrupt_path, remove_error);

        const auto symlink_path = std::filesystem::path(save_path.string() + ".symlink");
        std::error_code symlink_error;
        std::filesystem::create_symlink(save_path, symlink_path, symlink_error);
        if (!symlink_error) {
            World symlink_reader(999U, 2);
            expect_throw([&] { symlink_reader.load(symlink_path); });
            std::filesystem::remove(symlink_path, remove_error);
        }
    }
    std::filesystem::remove(save_path, remove_error);

    std::cout << "MineCPP world tests passed\n";
    return 0;
}
