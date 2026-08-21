// Tools/CLI/InitCommand.cpp — `akeir project init`: 빈 프로젝트 생성. 설계 문서 §5 (프로젝트 구조), §5.3 (canonical), §6 (world 문서), §7 (id), §88.3 (input.json).
//
//   akeir project init <name> [--dir DIR] [--tick-rate 60] [--seed S] [--force] [--json]
//     DIR 기본값 = ./<name>. 이미 비어 있지 않으면 DIR_NOT_EMPTY (--force 로 덮어쓰기 허용; 기존 파일은 지우지 않고 같은 이름만 덮어쓴다).
//   만드는 것:
//     project.json                  { name, tickRate, seed, defaultWorld, writable }
//     Worlds/Main.world.json        entities: MainCamera (Transform + Camera2D primary)
//     Prefabs/  Data/  Assets/  Tests/   (빈 디렉터리 + .gitkeep)
//     Config/input.json             MoveX/MoveY(axis, WASD·화살표) + Attack(Space) — 내장 PlayerController 가 읽는 action 이름
//     .gitignore                    Cache/  Tests/.results/
//     README.md                     다음에 할 명령 (entity create / prefab create / run / test / mcp)
//   이 command 는 CommandBus 를 거치지 않는다 — 아직 프로젝트가 없기 때문. 만든 뒤 `akeir validate` 를 돌려 결과를 envelope 에 싣는다.
#include "Commands.h"
#include "GameSystems.h"
#include "akeir/core/Id.h"
#include "akeir/reflection/Registry.h"
#include "akeir/runtime/Components.h"
#include "akeir/runtime/Project.h"
#include "akeir/serialization/Canonical.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace akeir::cli {

namespace {

const char* kDefaultInputJson = R"json({
  "$schema": "game://schema/input/1",
  "schemaVersion": 1,
  "actions": {
    "MoveX": { "type": "axis", "bindings": [ { "keys": ["A", "D"], "scale": [-1, 1] }, { "keys": ["Left", "Right"], "scale": [-1, 1] } ] },
    "MoveY": { "type": "axis", "bindings": [ { "keys": ["S", "W"], "scale": [-1, 1] }, { "keys": ["Down", "Up"], "scale": [-1, 1] } ] },
    "Attack": { "type": "button", "bindings": [ { "key": "Space" } ] }
  }
})json";

bool writeText(const fs::path& p, const std::string& text, std::string* err) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) { if (err) *err = "cannot write " + p.generic_string(); return false; }
    out << text;
    return static_cast<bool>(out);
}

std::string projectReadme(const std::string& name) {
    return "# " + name + " — AKEIR Engine project\n\n"
           "Authoring data lives in this directory as canonical JSON (Worlds/, Prefabs/, Config/). Change Worlds/Prefabs/Config through the `akeir` CLI (or the MCP server), not by hand; test scenarios in Tests/ are written by hand (format: Engine/Testing/README.md in the engine folder).\n"
           "`akeir` below means the engine's bin\\akeir.exe — add that folder to PATH or use the full path.\n\n"
           "```bash\n"
           "akeir validate --json                                   # checks ids, schemas, refs; exit 3 + fixes on errors\n"
           "akeir schema --all --json                               # every component and its properties\n"
           "akeir prefab create Hero --components \"{\\\"Collider2D\\\":{\\\"shape\\\":\\\"circle\\\",\\\"radius\\\":0.4},\\\"RigidBody2D\\\":{\\\"type\\\":\\\"dynamic\\\"},\\\"Movement\\\":{\\\"speed\\\":5},\\\"PlayerController\\\":{}}\" --json\n"
           "akeir prefab instantiate name:Hero --name Player --position 0,0,0 --json\n"
           "akeir entity create Wall --components \"{\\\"Collider2D\\\":{\\\"size\\\":[10,1]},\\\"RigidBody2D\\\":{\\\"type\\\":\\\"static\\\"},\\\"Transform\\\":{\\\"position\\\":[0,-5,0]}}\" --json   # a collider needs a (static) RigidBody2D to block anything\n"
           "akeir run --headless --ticks 600 --json                 # deterministic run, result.finalHash\n"
           "akeir run                                                # window (SDL build): WASD / arrows, ESC to quit\n"
           "akeir capture --ticks 120 --out Cache/capture/f.png --json\n"
           "akeir test --json                                        # Tests/**/*.test.json\n"
           "akeir undo --json                                        # every change above is one undo step\n"
           "akeir mcp                                                # MCP server over stdio for AI clients\n"
           "```\n\n"
           "Start from `akeir capabilities --json` (tools, busCommands, error codes) and `akeir project info --json`. Selectors: id | bare name | name:<n> | path:<World/Parent/Child>.\n"
           "Combat needs Health on BOTH sides: EnemyAI only damages targets that carry a Health component.\n"
           "Cache/ holds derived data (undo history, crash dumps, captures) and can be deleted.\n";
}

Envelope cmdProjectInit(Context& ctx) {
    registerBuiltinComponents();
    game::registerGameComponents();
    const std::string name = ctx.args.positional(2, "");
    if (name.empty()) return Envelope::failure("project.init", CommandError::make(ErrorCategory::Usage, "USAGE_ERROR", "akeir project init <name> [--dir DIR] [--tick-rate 60] [--seed S] [--force]"));
    for (char c : name) if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == ' '))
        return Envelope::failure("project.init", CommandError::make(ErrorCategory::Usage, "USAGE_ERROR", "Project name may contain letters, digits, space, '_' and '-'.", Json{{"name", name}}));
    fs::path dir = fs::absolute(ctx.args.getOr("dir", name));
    const int tickRate = static_cast<int>(ctx.args.getInt("tick-rate").value_or(60));
    if (tickRate <= 0 || tickRate > 1000) return Envelope::failure("project.init", CommandError::make(ErrorCategory::Usage, "USAGE_ERROR", "--tick-rate must be 1..1000."));
    std::error_code ec;
    if (fs::exists(dir, ec) && !fs::is_empty(dir, ec) && !ctx.args.has("force"))
        return Envelope::failure("project.init", CommandError::make(ErrorCategory::Conflict, "DIR_NOT_EMPTY", dir.generic_string() + " is not empty. Pick another --dir or pass --force to write into it.", Json{{"dir", dir.generic_string()}}));
    if (fs::exists(dir / "project.json", ec) && !ctx.args.has("force"))
        return Envelope::failure("project.init", CommandError::make(ErrorCategory::Conflict, "PROJECT_EXISTS", "A project.json already exists there.", Json{{"dir", dir.generic_string()}}));

    for (const char* sub : {"Worlds", "Prefabs", "Config", "Tests", "Data", "Assets"}) fs::create_directories(dir / sub, ec);

    Project prj = Project::create(dir.generic_string(), name, tickRate);
    std::uint64_t seed = 0;
    if (auto s = ctx.args.getInt("seed")) seed = static_cast<std::uint64_t>(*s);
    else { Id tmp = Id::generate("seed"); std::uint64_t h = 1469598103934665603ULL; for (char c : tmp.str()) h = (h ^ static_cast<unsigned char>(c)) * 1099511628211ULL; seed = h % 1000000007ULL; }
    prj.projectJsonMut()["seed"] = seed;

    // Main world: MainCamera
    const std::string worldId = Id::generate("world").str();
    const std::string camId = Id::generate("entity").str();
    Json world = Json::object();
    world["$schema"] = "game://schema/world/1";
    world["schemaVersion"] = 1;
    world["id"] = worldId;
    world["name"] = "Main";
    Json cam = Json::object();
    cam["name"] = "MainCamera";
    cam["parent"] = nullptr;
    cam["order"] = "a0";
    Json comps = Json::object();
    comps["Transform"] = Registry::global().find("Transform")->defaultJson(Visibility::Authoring);
    comps["Camera2D"] = Registry::global().find("Camera2D")->defaultJson(Visibility::Authoring);
    cam["components"] = comps;
    world["entities"] = Json::object();
    world["entities"][camId] = cam;
    prj.setDocument("Worlds/Main.world.json", world);
    prj.projectJsonMut()["defaultWorld"] = worldId;

    auto failed = prj.saveAll();
    if (!failed.empty()) return Envelope::failure("project.init", CommandError::make(ErrorCategory::Internal, "SAVE_FAILED", "Could not write: " + failed.front(), Json{{"failed", failed}}));

    std::string err;
    std::vector<std::string> written = {"project.json", "Worlds/Main.world.json"};
    auto put = [&](const char* rel, const std::string& text) { if (writeText(dir / rel, text, &err)) written.push_back(rel); };
    if (auto j = parseJson(kDefaultInputJson)) { if (auto text = canonicalDump(*j)) put("Config/input.json", *text); }
    put(".gitignore", "# AKEIR Engine derived data (undo history, journal, crash dumps, captures, serve lock) — safe to delete\nCache/\nTests/.results/\n");
    put("README.md", projectReadme(name));
    for (const char* keep : {"Prefabs/.gitkeep", "Tests/.gitkeep", "Data/.gitkeep", "Assets/.gitkeep"}) put(keep, "");

    // 검증
    std::vector<Diagnostic> diags;
    auto back = Project::load(dir.generic_string(), diags);
    Json validation = Json::object();
    if (back) { auto v = back->validate(); diags.insert(diags.end(), v.begin(), v.end()); }
    validation["summary"] = summarize(diags).toJson();

    Json r = Json{{"name", name}, {"dir", dir.generic_string()}, {"tickRate", tickRate}, {"seed", seed}, {"defaultWorld", worldId}, {"camera", camId}, {"files", written}, {"validation", validation},
                  {"next", Json::array({"cd " + dir.generic_string(), "akeir capabilities --json", "akeir schema --all --json", "akeir prefab create <Name> --components {...} --json", "akeir run --headless --ticks 600 --json"})}};
    Envelope env = Envelope::success("project.init", r);
    for (auto& d : diags) env.withWarning(d);
    return env;
}

} // namespace

void registerInitCommands(std::vector<CommandSpec>& t) {
    t.push_back({"project.init", {"project", "init"}, "Mutation", "Create an empty project",
                 "Writes project.json, Worlds/Main.world.json (MainCamera), Config/input.json (MoveX/MoveY/Attack), empty Prefabs/Tests/Data/Assets, .gitignore and README. Does not need an existing project.",
                 "akeir project init <name> [--dir DIR] [--tick-rate 60] [--seed S] [--force] [--json]", false, false, false, cmdProjectInit});
}

} // namespace akeir::cli
