#pragma once
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
namespace hiface_linux_demo {
// Shared file I/O has no SDK dependency, so startup/settings UI can remain usable.
// 公共文件读写不依赖 SDK，启动失败时设置与错误页面仍可工作。
inline void atomicWrite(const std::string& path, const std::string& contents) {
    const auto temporary = path + ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    try {
        errno = 0;
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::filesystem::filesystem_error("Cannot write file", path,
            std::error_code(errno ? errno : EIO, std::generic_category()));
        output.write(contents.data(), contents.size());
        const int write_error = output ? 0 : errno;
        output.close();
        if (!output) throw std::filesystem::filesystem_error("Cannot write file", path,
            std::error_code(write_error ? write_error : errno ? errno : EIO, std::generic_category()));
        std::filesystem::rename(temporary, path);
    } catch (...) { std::error_code ignored; std::filesystem::remove(temporary, ignored); throw; }
}
}
