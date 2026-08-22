// Tools/CLI/ResidentRun.h — a play world kept alive inside `akeir serve` / `akeir mcp` (ADR-0041).
//
//   run open → run step (N ticks, scripted input) → run inspect / query / snapshot … → run close
//
// The one-shot `run --headless` rebuilds the world on every call; a resident run lets an AI (or a test harness)
// advance the same world tick by tick and look at runtime state in between — the "observe while playing" loop the
// CatSurvivor feedback asked for. Runs are owned by the ServeHost and die with it (or on `run close`).
#pragma once

#include "akeir/core/Time.h"
#include "akeir/ecs/PlayWorld.h"
#include "akeir/runtime/Project.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace akeir::cli {

struct ResidentRun {
    std::string id;                    // run_…
    std::string worldId;
    std::uint64_t seed = 0;
    std::optional<Project> project;    // copy taken at open (selectors resolve against it)
    std::unique_ptr<PlayWorld> world;
    SimTime sim;                       // next tick to simulate
    std::int64_t ticksRun = 0;
    std::string openedAt;
};

using ResidentRuns = std::map<std::string, std::unique_ptr<ResidentRun>>;

} // namespace akeir::cli
