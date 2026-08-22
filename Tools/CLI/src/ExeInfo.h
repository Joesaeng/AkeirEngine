// Tools/CLI/ExeInfo.h — identity of an executable file (ADR-0034): which build is actually running?
//
//   ownExePath()        → "C:/…/build/msvc-release/bin/akeir.exe" (the path the process was started from; unchanged if the file is renamed later)
//   exeStamp(path)      → {exists, bytes, mtimeNanos} — cheap change probe (no content read)
//   fileSha256(path)    → "sha256:<64 hex>" or "" when unreadable
//   ownExeInfoJson()    → {path, bytes, modified, sha256} (sha256 computed once, cached) — `akeir version`, capabilities.info.exe
//
// The MCP adapter compares exeStamp()/fileSha256() of the launch path against the worker it spawned to decide
// when a rebuilt akeir.exe must replace the running worker (Tools/CLI/src/McpAdapter.cpp).
#pragma once

#include "akeir/core/Json.h"

#include <cstdint>
#include <string>

namespace akeir::cli {

struct ExeStamp {
    bool exists = false;
    std::uint64_t bytes = 0;
    std::uint64_t mtimeNanos = 0;
    bool operator==(const ExeStamp&) const = default;
};

std::string ownExePath();
ExeStamp exeStamp(const std::string& path);
std::string fileSha256(const std::string& path);
Json ownExeInfoJson();

} // namespace akeir::cli
