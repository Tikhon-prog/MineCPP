#pragma once
#include <string>
#include <vector>

namespace minecpp {
int run_process(const std::string& executable, const std::vector<std::string>& arguments, const std::string& working_directory);
std::string find_java();
}
