#include "process.hpp"

#include <cstdlib>
#include <filesystem>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace minecpp {

std::string find_java() {
    if (const char* home = std::getenv("JAVA_HOME")) {
        const auto candidate = std::filesystem::path(home) / "bin" /
#ifdef _WIN32
            "java.exe";
#else
            "java";
#endif
        if (std::filesystem::exists(candidate)) return candidate.string();
    }
#ifdef _WIN32
    return "java.exe";
#else
    return "java";
#endif
}

#ifdef _WIN32
static std::wstring widen(const std::string& text) {
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1, nullptr, 0);
    if (count <= 0) throw std::runtime_error("Cannot convert process argument to UTF-16");
    std::wstring value(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1, value.data(), count);
    value.pop_back();
    return value;
}
static std::wstring quote_windows(const std::wstring& arg) {
    if (arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    unsigned int slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') { out.append(slashes * 2 + 1, L'\\'); out.push_back(c); slashes = 0; continue; }
        out.append(slashes, L'\\'); slashes = 0; out.push_back(c);
    }
    out.append(slashes * 2, L'\\'); out.push_back(L'\"');
    return out;
}
int run_process(const std::string& executable, const std::vector<std::string>& arguments, const std::string& working_directory) {
    std::wstring command = quote_windows(widen(executable));
    for (const auto& arg : arguments) { command.push_back(L' '); command += quote_windows(widen(arg)); }
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const std::wstring cwd = widen(working_directory);
    if (!CreateProcessW(widen(executable).c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        cwd.empty() ? nullptr : cwd.c_str(), &startup, &process))
        throw std::runtime_error("CreateProcess failed with Windows error " + std::to_string(GetLastError()));
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return static_cast<int>(exit_code);
}
#else
int run_process(const std::string& executable, const std::vector<std::string>& arguments, const std::string& working_directory) {
    const pid_t pid = fork();
    if (pid < 0) throw std::runtime_error(std::string("fork failed: ") + std::strerror(errno));
    if (pid == 0) {
        if (!working_directory.empty() && chdir(working_directory.c_str()) != 0) _exit(126);
        std::vector<std::string> storage;
        storage.reserve(arguments.size() + 1);
        storage.push_back(executable);
        storage.insert(storage.end(), arguments.begin(), arguments.end());
        std::vector<char*> argv;
        argv.reserve(storage.size() + 1);
        for (auto& item : storage) argv.push_back(item.data());
        argv.push_back(nullptr);
        execvp(executable.c_str(), argv.data());
        _exit(errno == ENOENT ? 127 : 126);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) continue;
        throw std::runtime_error(std::string("waitpid failed: ") + std::strerror(errno));
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}
#endif

} // namespace minecpp
