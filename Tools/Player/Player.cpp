// Tools/Player/Player.cpp — the game executable (`build/<preset>/bin/<ProjectName>.exe`).
//
// Double-click → window → play until it is closed. It is the windowed `akeir run` without the terminal:
// same engine, same Game/Source systems, same Config/input.json. No console window; logs go to
// <project>/Cache/player.log and fatal errors are shown in a message box.
//
// Project lookup order: --project DIR → project.json next to the exe (shipped-folder layout) →
// walking up from the exe directory looking for Game/project.json or project.json (repo/zip layout:
// build/<preset>/bin → <root>/Game) → the current directory.
//
// Optional arguments (useful for smoke tests): --ticks N (auto-close), --width W --height H,
// --video-driver dummy, --record inputs.jsonl, --world <selector>, --seed N.
#include "GameSystems.h"
#include "akeir/core/Crash.h"
#include "akeir/core/ExitCodes.h"
#include "akeir/core/FpEnv.h"
#include "akeir/core/Log.h"
#include "akeir/ecs/PlayWorld.h"
#include "akeir/platform/InputMap.h"
#include "akeir/platform/Interactive.h"
#include "akeir/platform/Platform.h"
#include "akeir/render/Renderer2D.h"
#include "akeir/runtime/Application.h"
#include "akeir/runtime/Project.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
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
using namespace akeir;

namespace {

struct Args {
    std::map<std::string, std::string> opts;
    bool has(const char* k) const { return opts.count(k) > 0; }
    std::string get(const char* k, const std::string& def = "") const { auto it = opts.find(k); return it == opts.end() ? def : it->second; }
    long long getInt(const char* k, long long def) const { auto it = opts.find(k); return it == opts.end() ? def : std::strtoll(it->second.c_str(), nullptr, 10); }
};

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        if (s.rfind("--", 0) != 0) continue;
        std::string key = s.substr(2), value = "1";
        if (auto eq = key.find('='); eq != std::string::npos) { value = key.substr(eq + 1); key = key.substr(0, eq); }
        else if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) value = argv[++i];
        a.opts[key] = value;
    }
    return a;
}

fs::path exeDir() {
#ifdef _WIN32
    wchar_t buf[32768];
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return n ? fs::path(std::wstring(buf, n)).parent_path() : fs::current_path();
#else
    std::error_code ec;
    return fs::read_symlink("/proc/self/exe", ec).parent_path();
#endif
}

std::string findProject(const Args& a) {
    std::error_code ec;
    if (a.has("project")) return fs::absolute(a.get("project"), ec).generic_string();
    fs::path dir = exeDir();
    for (int depth = 0; depth < 8 && !dir.empty(); ++depth) {
        if (fs::exists(dir / "project.json", ec)) return dir.generic_string();
        if (fs::exists(dir / "Game" / "project.json", ec)) return (dir / "Game").generic_string();
        if (!dir.has_parent_path() || dir.parent_path() == dir) break;
        dir = dir.parent_path();
    }
    dir = fs::current_path(ec);
    for (int depth = 0; depth < 8 && !dir.empty(); ++depth) {
        if (fs::exists(dir / "project.json", ec)) return dir.generic_string();
        if (!dir.has_parent_path() || dir.parent_path() == dir) break;
        dir = dir.parent_path();
    }
    return "";
}

int fail(const std::string& title, const std::string& text) {
    AKEIR_LOG(Error, "player", "fatal", text, Json{{"title", title}});
    Logger::global().flush();
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), text.c_str(), nullptr);
    return kExitCommandFailed;
}

} // namespace

int main(int argc, char** argv) {
    Args args = parseArgs(argc, argv);
    std::string projectDir = findProject(args);
    if (projectDir.empty())
        return fail("AKEIR player", "No project found. Put this executable next to a project.json, keep it in build/<preset>/bin of a checkout that has Game/project.json, or pass --project <dir>.");

    // logs: file only (there is no console); crash dumps next to the project like the CLI
    Logger::global().clearSinks();
    std::error_code ec;
    fs::create_directories(fs::path(projectDir) / "Cache", ec);
    Logger::global().addSink(makeFileSink((fs::path(projectDir) / "Cache" / "player.log").string()));
    CrashConfig cc;
    cc.command = "player";
    cc.stem = "player";
    cc.dumpDir = (fs::path(projectDir) / "Cache" / "crash").string();
    installCrashHandler(cc);

    game::registerGameComponents();
    std::vector<Diagnostic> diags;
    auto prj = Project::load(projectDir, diags);
    if (!prj) return fail("AKEIR player", "Cannot load the project in " + projectDir + ". Run `akeir validate` there.");

    std::string worldId;
    if (args.has("world")) {
        for (const auto& id : prj->resolveSelector(args.get("world"))) if (auto l = prj->locate(id); l && l->kind == "world") worldId = id;
        if (worldId.empty()) return fail(prj->name(), "--world '" + args.get("world") + "' does not name a world.");
    } else {
        auto dw = prj->defaultWorld();
        if (!dw) return fail(prj->name(), "The project has no world to play (project.json defaultWorld).");
        worldId = *dw;
    }

    normalizeFpEnv();
    PlatformConfig pc;
    pc.width = static_cast<int>(args.getInt("width", 1280));
    pc.height = static_cast<int>(args.getInt("height", 720));
    pc.title = prj->name();
    pc.videoDriver = args.get("video-driver", "");
    std::string err;
    auto platform = Platform::init(pc, &err);
    if (!platform) return fail(prj->name(), "SDL could not open a window: " + err);

    PlayWorldConfig cfg;
    cfg.seed = args.has("seed") ? static_cast<std::uint64_t>(args.getInt("seed", 0)) : prj->seed();
    cfg.tickRate = prj->tickRate();
    std::vector<Diagnostic> bd;
    auto world = PlayWorld::build(*prj, worldId, cfg, bd);
    if (!world) {
        std::string text = "The world could not be built. Run `akeir validate` in " + projectDir + ".";
        for (const auto& d : bd) text += "\n- " + d.ruleId + ": " + d.message.text;
        return fail(prj->name(), text);
    }
    game::registerGameSystems(*world);

    std::vector<Diagnostic> inputDiags;
    InputMap input = InputMap::loadFile((fs::path(projectDir) / "Config" / "input.json").string(), &inputDiags);
    for (const auto& d : inputDiags) AKEIR_LOG(Warn, "player", "input", d.message.text, Json{{"ruleId", d.ruleId}});

    auto renderer = Renderer2D::createForWindow(platform->window(), &err);
    if (!renderer) return fail(prj->name(), "Renderer failed: " + err);

    InteractiveConfig ic;
    ic.maxTicks = args.getInt("ticks", 0);
    ic.tickRate = prj->tickRate();
    ic.recordInputsPath = args.get("record", "");
    AKEIR_LOG(Info, "player", "start", "Playing.", Json{{"project", projectDir}, {"world", worldId}, {"seed", cfg.seed}, {"videoDriver", platform->currentVideoDriver()}, {"renderer", renderer->backendName()}});
    InteractiveResult ir = runInteractive(*platform, *renderer, *world, input, ic);
    AKEIR_LOG(Info, "player", "stop", "Stopped.", ir.toJson());
    Logger::global().flush();
    return kExitOk;
}
