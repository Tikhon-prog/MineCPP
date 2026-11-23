#include "world.hpp"

#include "mesh.hpp"
#include "math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace minecpp {
namespace {

std::uint64_t mix64(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

std::uint64_t coordinate_hash(std::uint32_t seed, int x, int z) {
    std::uint64_t value = (static_cast<std::uint64_t>(seed) << 32U) ^
                          static_cast<std::uint32_t>(x);
    value ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(z)) * 0x9e3779b185ebca87ULL;
    return mix64(value);
}

float random_value(std::uint32_t seed, int x, int z) {
    const std::uint64_t hash = coordinate_hash(seed, x, z);
    const std::uint32_t bits = static_cast<std::uint32_t>(hash >> 40U);
    return static_cast<float>(bits) / 8388607.5F - 1.0F;
}

float smooth(float value) { return value * value * (3.0F - 2.0F * value); }
float lerp(float a, float b, float amount) { return a + (b - a) * amount; }

float value_noise(std::uint32_t seed, float x, float z) {
    const int x0 = static_cast<int>(std::floor(x));
    const int z0 = static_cast<int>(std::floor(z));
    const float tx = smooth(x - static_cast<float>(x0));
    const float tz = smooth(z - static_cast<float>(z0));
    const float a = lerp(random_value(seed, x0, z0), random_value(seed, x0 + 1, z0), tx);
    const float b = lerp(random_value(seed, x0, z0 + 1), random_value(seed, x0 + 1, z0 + 1), tx);
    return lerp(a, b, tz);
}

bool within_chunk(int value, int chunk_coordinate) {
    return floor_div(value, kChunkSize) == chunk_coordinate;
}

void write_u32(std::ostream& out, std::uint32_t value) {
    const char bytes[4]{
        static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU),
        static_cast<char>((value >> 16U) & 0xffU),
        static_cast<char>((value >> 24U) & 0xffU)
    };
    out.write(bytes, 4);
}

std::uint32_t read_u32(std::istream& in) {
    unsigned char bytes[4]{};
    in.read(reinterpret_cast<char*>(bytes), 4);
    if (!in) throw std::runtime_error("World save is truncated");
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

std::size_t worker_count() {
    const unsigned int reported = std::thread::hardware_concurrency();
    const unsigned int useful = reported > 1 ? reported - 1 : 2;
    return static_cast<std::size_t>(std::clamp(useful, 2U, 8U));
}

} // namespace

World::World(std::uint32_t seed, int render_distance)
    : workers_(worker_count()), seed_(seed), render_distance_(std::clamp(render_distance, 2, 8)) {}

World::~World() {
    workers_.shutdown();
}

int World::terrain_height(std::uint32_t seed, int world_x, int world_z) {
    const float x = static_cast<float>(world_x);
    const float z = static_cast<float>(world_z);
    const float continental = value_noise(seed, x / 112.0F, z / 112.0F) * 17.0F;
    const float hills = value_noise(seed ^ 0x5f356495U, x / 34.0F, z / 34.0F) * 8.0F;
    const float detail = value_noise(seed ^ 0x9e3779b9U, x / 13.0F, z / 13.0F) * 2.5F;
    return std::clamp(static_cast<int>(std::lround(36.0F + continental + hills + detail)), 7, 82);
}

std::vector<Block> World::generate_chunk_blocks(
    ChunkPos position, std::uint32_t seed, const std::vector<BlockEdit>& edits) {

    std::vector<Block> blocks(static_cast<std::size_t>(kChunkSize) * kWorldHeight * kChunkSize, Block::Air);
    const int world_base_x = position.x * kChunkSize;
    const int world_base_z = position.z * kChunkSize;
    std::array<int, kChunkSize * kChunkSize> heights{};

    for (int z = 0; z < kChunkSize; ++z) {
        for (int x = 0; x < kChunkSize; ++x) {
            const int height = terrain_height(seed, world_base_x + x, world_base_z + z);
            heights[static_cast<std::size_t>(z * kChunkSize + x)] = height;
            const Block surface = height <= kSeaLevel + 2 ? Block::Sand : Block::Grass;
            for (int y = 0; y <= height; ++y) {
                Block block = Block::Stone;
                if (y == 0) block = Block::Bedrock;
                else if (y == height) block = surface;
                else if (y >= height - 4) block = surface == Block::Sand ? Block::Sand : Block::Dirt;
                blocks[block_index(x, y, z)] = block;
            }
        }
    }

    // Small deterministic tree clusters, kept away from chunk borders.
    for (int z = 3; z < kChunkSize - 3; ++z) {
        for (int x = 3; x < kChunkSize - 3; ++x) {
            const int world_x = world_base_x + x;
            const int world_z = world_base_z + z;
            const int height = heights[static_cast<std::size_t>(z * kChunkSize + x)];
            if (height <= kSeaLevel + 3 || coordinate_hash(seed ^ 0x73a4b12dU, world_x, world_z) % 137ULL != 0ULL)
                continue;
            const int trunk_height = 4 + static_cast<int>(coordinate_hash(seed, world_x, world_z) % 2ULL);
            for (int dy = 1; dy <= trunk_height; ++dy)
                blocks[block_index(x, height + dy, z)] = Block::Wood;
            for (int dy = trunk_height - 1; dy <= trunk_height + 1; ++dy) {
                const int radius = dy == trunk_height + 1 ? 1 : 2;
                for (int dz = -radius; dz <= radius; ++dz) {
                    for (int dx = -radius; dx <= radius; ++dx) {
                        if (std::abs(dx) == radius && std::abs(dz) == radius && radius == 2) continue;
                        const int lx = x + dx;
                        const int lz = z + dz;
                        const int ly = height + dy;
                        if (lx >= 0 && lx < kChunkSize && lz >= 0 && lz < kChunkSize && ly < kWorldHeight) {
                            auto& cell = blocks[block_index(lx, ly, lz)];
                            if (cell == Block::Air) cell = Block::Leaves;
                        }
                    }
                }
            }
        }
    }

    for (const BlockEdit& edit : edits) {
        if (edit.position.y < 0 || edit.position.y >= kWorldHeight ||
            !within_chunk(edit.position.x, position.x) || !within_chunk(edit.position.z, position.z))
            continue;
        const int local_x = positive_mod(edit.position.x, kChunkSize);
        const int local_z = positive_mod(edit.position.z, kChunkSize);
        blocks[block_index(local_x, edit.position.y, local_z)] = edit.block;
    }
    return blocks;
}

void World::load(const std::filesystem::path& path, bool keep_command_line_seed) {
    if (!std::filesystem::exists(path)) return;
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open world save: " + path.string());
    char magic[8]{};
    in.read(magic, 8);
    if (!in || std::memcmp(magic, "MCPPWLD1", 8) != 0)
        throw std::runtime_error("Invalid MineCPP world save: " + path.string());

    const std::uint32_t saved_seed = read_u32(in);
    const std::uint32_t count = read_u32(in);
    if (count > 10000000U) throw std::runtime_error("World save contains too many block edits");
    if (!keep_command_line_seed) seed_ = saved_seed;
    edits_.clear();
    for (std::uint32_t i = 0; i < count; ++i) {
        const int x = static_cast<std::int32_t>(read_u32(in));
        const int y = static_cast<std::int32_t>(read_u32(in));
        const int z = static_cast<std::int32_t>(read_u32(in));
        char raw_block = 0;
        in.read(&raw_block, 1);
        if (!in) throw std::runtime_error("World save ended while reading block edits");
        const auto id = static_cast<unsigned char>(raw_block);
        if (y >= 0 && y < kWorldHeight && id <= static_cast<unsigned char>(Block::Bedrock))
            edits_.insert_or_assign(BlockPos{x, y, z}, static_cast<Block>(id));
    }
}

void World::save(const std::filesystem::path& path) const {
    const auto parent = path.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    const std::filesystem::path temporary = path.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("Cannot create world save: " + temporary.string());
        out.write("MCPPWLD1", 8);
        write_u32(out, seed_);
        if (edits_.size() > std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("Too many block edits to save");
        write_u32(out, static_cast<std::uint32_t>(edits_.size()));
        for (const auto& [position, block] : edits_) {
            write_u32(out, static_cast<std::uint32_t>(position.x));
            write_u32(out, static_cast<std::uint32_t>(position.y));
            write_u32(out, static_cast<std::uint32_t>(position.z));
            const char id = static_cast<char>(block);
            out.write(&id, 1);
        }
        if (!out) throw std::runtime_error("Failed writing world save: " + temporary.string());
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
        if (error) throw std::runtime_error("Cannot replace world save: " + error.message());
    }
}

void World::request_chunk(ChunkPos position) {
    if (chunks_.contains(position) || pending_generation_.contains(position)) return;
    pending_generation_.insert(position);
    std::vector<BlockEdit> local_edits;
    for (const auto& [block_position, block] : edits_) {
        if (within_chunk(block_position.x, position.x) && within_chunk(block_position.z, position.z))
            local_edits.push_back({block_position, block});
    }
    const std::uint32_t seed = seed_;
    workers_.submit([this, position, seed, local_edits = std::move(local_edits)]() mutable {
        GeneratedChunk result{position, generate_chunk_blocks(position, seed, local_edits)};
        std::lock_guard lock(results_mutex_);
        generated_chunks_.push(std::move(result));
    });
}

bool World::should_keep(ChunkPos position) const {
    return std::abs(position.x - centre_x_) <= render_distance_ + 2 &&
           std::abs(position.z - centre_z_) <= render_distance_ + 2;
}

void World::update_streaming(int block_x, int block_z) {
    centre_x_ = floor_div(block_x, kChunkSize);
    centre_z_ = floor_div(block_z, kChunkSize);

    std::vector<ChunkPos> wanted;
    for (int dz = -render_distance_; dz <= render_distance_; ++dz)
        for (int dx = -render_distance_; dx <= render_distance_; ++dx)
            wanted.push_back({centre_x_ + dx, centre_z_ + dz});
    std::sort(wanted.begin(), wanted.end(), [this](const ChunkPos& a, const ChunkPos& b) {
        const int ax = a.x - centre_x_, az = a.z - centre_z_;
        const int bx = b.x - centre_x_, bz = b.z - centre_z_;
        return ax * ax + az * az < bx * bx + bz * bz;
    });
    for (const ChunkPos position : wanted) request_chunk(position);

    for (auto it = chunks_.begin(); it != chunks_.end();) {
        if (!should_keep(it->first)) it = chunks_.erase(it);
        else ++it;
    }
}

ChunkSnapshot World::make_snapshot(ChunkPos position) const {
    ChunkSnapshot snapshot;
    snapshot.position = position;
    snapshot.blocks.resize(static_cast<std::size_t>(kWorldHeight) *
                           (kChunkSize + 2) * (kChunkSize + 2), Block::Air);
    for (int y = 0; y < kWorldHeight; ++y) {
        for (int z = -1; z <= kChunkSize; ++z) {
            for (int x = -1; x <= kChunkSize; ++x) {
                const int world_x = position.x * kChunkSize + x;
                const int world_z = position.z * kChunkSize + z;
                const auto index = (static_cast<std::size_t>(y) * (kChunkSize + 2) +
                                    static_cast<std::size_t>(z + 1)) * (kChunkSize + 2) +
                                   static_cast<std::size_t>(x + 1);
                snapshot.blocks[index] = get_block(world_x, y, world_z);
            }
        }
    }
    return snapshot;
}

void World::schedule_mesh(ChunkPos position) {
    auto it = chunks_.find(position);
    if (it == chunks_.end()) return;
    const std::uint64_t revision = ++it->second.mesh_revision;
    ChunkSnapshot snapshot = make_snapshot(position);
    workers_.submit([this, position, revision, snapshot = std::move(snapshot)]() mutable {
        GeneratedMesh result{position, revision, build_chunk_mesh(snapshot)};
        std::lock_guard lock(results_mutex_);
        generated_meshes_.push(std::move(result));
    });
}

void World::mark_neighbours_dirty(ChunkPos position) {
    schedule_mesh(position);
    schedule_mesh({position.x - 1, position.z});
    schedule_mesh({position.x + 1, position.z});
    schedule_mesh({position.x, position.z - 1});
    schedule_mesh({position.x, position.z + 1});
}

void World::pump_jobs() {
    std::queue<GeneratedChunk> new_chunks;
    std::queue<GeneratedMesh> new_meshes;
    {
        std::lock_guard lock(results_mutex_);
        std::swap(new_chunks, generated_chunks_);
        std::swap(new_meshes, generated_meshes_);
    }

    while (!new_chunks.empty()) {
        GeneratedChunk result = std::move(new_chunks.front());
        new_chunks.pop();
        pending_generation_.erase(result.position);
        if (!should_keep(result.position)) continue;
        Chunk chunk;
        chunk.position = result.position;
        chunk.blocks = std::move(result.blocks);
        chunks_.insert_or_assign(result.position, std::move(chunk));
        mark_neighbours_dirty(result.position);
    }

    while (!new_meshes.empty()) {
        GeneratedMesh result = std::move(new_meshes.front());
        new_meshes.pop();
        const auto it = chunks_.find(result.position);
        if (it == chunks_.end() || result.revision != it->second.mesh_revision) continue;
        it->second.mesh = std::move(result.vertices);
        it->second.mesh_ready = true;
    }
}

Block World::get_block(int x, int y, int z) const {
    if (y < 0 || y >= kWorldHeight) return Block::Air;
    const ChunkPos position{floor_div(x, kChunkSize), floor_div(z, kChunkSize)};
    const auto it = chunks_.find(position);
    if (it == chunks_.end() || it->second.blocks.empty()) return Block::Air;
    return it->second.blocks[block_index(positive_mod(x, kChunkSize), y, positive_mod(z, kChunkSize))];
}

bool World::is_chunk_loaded(int block_x, int block_z) const {
    return chunks_.contains({floor_div(block_x, kChunkSize), floor_div(block_z, kChunkSize)});
}

bool World::set_block(int x, int y, int z, Block block) {
    if (y < 0 || y >= kWorldHeight) return false;
    const ChunkPos position{floor_div(x, kChunkSize), floor_div(z, kChunkSize)};
    auto it = chunks_.find(position);
    if (it == chunks_.end()) return false;
    it->second.blocks[block_index(positive_mod(x, kChunkSize), y, positive_mod(z, kChunkSize))] = block;
    edits_.insert_or_assign(BlockPos{x, y, z}, block);
    schedule_mesh(position);
    const int local_x = positive_mod(x, kChunkSize);
    const int local_z = positive_mod(z, kChunkSize);
    if (local_x == 0) schedule_mesh({position.x - 1, position.z});
    if (local_x == kChunkSize - 1) schedule_mesh({position.x + 1, position.z});
    if (local_z == 0) schedule_mesh({position.x, position.z - 1});
    if (local_z == kChunkSize - 1) schedule_mesh({position.x, position.z + 1});
    return true;
}

bool World::has_spawn_chunk() const { return chunks_.contains({0, 0}); }

std::size_t World::pending_job_count() const { return pending_generation_.size(); }

} // namespace minecpp
