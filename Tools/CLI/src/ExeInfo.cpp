// Tools/CLI/ExeInfo.cpp — see ExeInfo.h (ADR-0034)
#include "ExeInfo.h"

#include "akeir/core/Hash.h"
#include "akeir/core/Time.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace fs = std::filesystem;

namespace akeir::cli {

std::string ownExePath() {
#ifdef _WIN32
    wchar_t buf[32768];
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0) return "";
    return fs::path(std::wstring(buf, n)).generic_string();
#else
    std::error_code ec;
    return fs::read_symlink("/proc/self/exe", ec).generic_string();
#endif
}

ExeStamp exeStamp(const std::string& path) {
    ExeStamp s;
    std::error_code ec;
    fs::path p(path);
    if (!fs::is_regular_file(p, ec)) return s;
    s.exists = true;
    s.bytes = static_cast<std::uint64_t>(fs::file_size(p, ec));
    auto t = fs::last_write_time(p, ec);
    s.mtimeNanos = static_cast<std::uint64_t>(t.time_since_epoch().count());
    return s;
}

std::string fileSha256(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
    Sha256 h;
    std::vector<char> buf(1 << 16);
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        std::streamsize got = in.gcount();
        if (got > 0) h.update(buf.data(), static_cast<std::size_t>(got));
    }
    auto d = h.finish();
    return "sha256:" + toHex(d.data(), d.size());
}

Json ownExeInfoJson() {
    static std::mutex mtx;
    static Json cached;
    std::lock_guard<std::mutex> lock(mtx);
    if (!cached.is_null()) return cached;
    std::string path = ownExePath();
    ExeStamp st = exeStamp(path);
    Json j = Json::object();
    j["path"] = path;
    j["bytes"] = st.bytes;
    // file mtime → wall-clock ISO for humans (nanos since epoch of the filesystem clock are implementation-defined; keep the raw value too)
    j["mtimeNanos"] = st.mtimeNanos;
    j["sha256"] = fileSha256(path);
    cached = j;
    return cached;
}

} // namespace akeir::cli
