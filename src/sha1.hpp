#pragma once

#include <filesystem>
#include <string>

namespace minecpp {

[[nodiscard]] std::string sha1_file(const std::filesystem::path& path);

} // namespace minecpp
