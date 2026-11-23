#pragma once

#include "chunk.hpp"
#include "thread_pool.hpp"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace minecpp {

class World {
public:
    explicit World(std::uint32_t seed = 1337U, int render_distance = 4);
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    void load(const std::filesystem::path& path, bool keep_command_line_seed = false);
    void save(const std::filesystem::path& path) const;

    void update_streaming(int block_x, int block_z);
    void pump_jobs();

    [[nodiscard]] Block get_block(int x, int y, int z) const;
    [[nodiscard]] bool is_chunk_loaded(int block_x, int block_z) const;
    [[nodiscard]] bool set_block(int x, int y, int z, Block block);
    [[nodiscard]] bool has_spawn_chunk() const;
    [[nodiscard]] const auto& chunks() const { return chunks_; }

    [[nodiscard]] std::uint32_t seed() const { return seed_; }
    [[nodiscard]] int render_distance() const { return render_distance_; }
    [[nodiscard]] std::size_t loaded_chunk_count() const { return chunks_.size(); }
    [[nodiscard]] std::size_t pending_job_count() const;

    [[nodiscard]] static int terrain_height(std::uint32_t seed, int world_x, int world_z);
    [[nodiscard]] static std::vector<Block> generate_chunk_blocks(
        ChunkPos position, std::uint32_t seed, const std::vector<BlockEdit>& edits = {});

private:
    struct GeneratedChunk { ChunkPos position; std::vector<Block> blocks; };
    struct GeneratedMesh { ChunkPos position; std::uint64_t revision; std::vector<MeshVertex> vertices; };

    void request_chunk(ChunkPos position);
    void schedule_mesh(ChunkPos position);
    [[nodiscard]] ChunkSnapshot make_snapshot(ChunkPos position) const;
    [[nodiscard]] bool should_keep(ChunkPos position) const;
    void mark_neighbours_dirty(ChunkPos position);

    ThreadPool workers_;
    std::uint32_t seed_;
    int render_distance_;
    int centre_x_ = 0;
    int centre_z_ = 0;

    std::unordered_map<ChunkPos, Chunk, ChunkPosHash> chunks_;
    std::unordered_set<ChunkPos, ChunkPosHash> pending_generation_;
    std::unordered_map<BlockPos, Block, BlockPosHash> edits_;

    mutable std::mutex results_mutex_;
    std::queue<GeneratedChunk> generated_chunks_;
    std::queue<GeneratedMesh> generated_meshes_;
};

} // namespace minecpp
