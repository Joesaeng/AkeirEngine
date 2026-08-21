// Tools/CLI/SdlCommands.cpp — 렌더/창이 필요한 명령: `akeir capture`, `akeir run`(창 모드), `akeir input map`, 그리고 `akeir test` 의 capture hook.
// 설계 문서 §20 (창/driver), §20.1 (창 모드 accumulator), §27 (capture), §27.1 (golden 비교), §88.3 (input.json).
// AKEIR_WITH_SDL=OFF(msvc-headless) 빌드에서는 전부 FEATURE_UNAVAILABLE 을 돌려준다 — 같은 command 표를 유지해 capabilities 가 일관되게.
//
//   akeir capture [--ticks N] [--width W] [--height H] [--out f.png] [--compare golden.png] [--diff diff.png] [--per-pixel 0.1] [--max-mismatch 0.002] [--world W] [--seed S] [--json]
//     software renderer (CPU) 로 그린다 — 창/GPU 없이 결정적 PNG (ADR-0026). --compare 가 있으면 §27.1 비교 결과 + exit 3 on mismatch.
//   akeir run [--ticks N] [--record inputs.jsonl] [--width W] [--height H] [--world W] [--seed S]     (--headless 가 없으면 창 모드)
//   akeir input map [--json]        Config/input.json 이 SDL 에서 어떻게 해석되는지 (scancode, 미지원 바인딩)
#include "Commands.h"
#include "GameSystems.h"
#include "akeir/core/ExitCodes.h"
#include "akeir/core/FpEnv.h"
#include "akeir/core/Hash.h"
#include "akeir/core/Log.h"
#include "akeir/ecs/PlayWorld.h"
#include "akeir/runtime/Components.h"
#include "akeir/testing/TestRunner.h"

#ifdef AKEIR_HAS_SDL
#include "akeir/platform/Interactive.h"
#include "akeir/platform/InputMap.h"
#include "akeir/platform/Platform.h"
#include "akeir/render/Renderer2D.h"
#endif

#include <filesystem>

namespace akeir::cli {

namespace {

Envelope unavailable(const std::string& command) {
    return Envelope::failure(command, CommandError::make(ErrorCategory::Precondition, "FEATURE_UNAVAILABLE",
        "This build has no SDL3 (AKEIR_WITH_SDL=OFF, preset msvc-headless). Build the msvc-debug preset for capture / windowed run.", Json{{"preset", "msvc-debug"}}));
}

bool buildWorld(Context& ctx, Project& prj, const std::string& command, Envelope& fail, std::unique_ptr<PlayWorld>& out) {
    std::string sel = ctx.args.getOr("world", "");
    std::string worldId;
    if (sel.empty()) { auto dw = prj.defaultWorld(); if (!dw) { fail = Envelope::failure(command, CommandError::make(ErrorCategory::NotFound, "WORLD_NOT_FOUND", "Project has no worlds.")); return false; } worldId = *dw; }
    else {
        std::vector<std::string> worlds;
        for (const auto& id : prj.resolveSelector(sel)) if (auto l = prj.locate(id); l && l->kind == "world") worlds.push_back(id);
        if (worlds.size() != 1) { fail = Envelope::failure(command, CommandError::make(ErrorCategory::NotFound, "WORLD_NOT_FOUND", "--world '" + sel + "' does not match exactly one world.", Json{{"candidates", worlds}})); return false; }
        worldId = worlds.front();
    }
    PlayWorldConfig cfg;
    auto seed = ctx.args.getInt("seed");
    cfg.seed = seed ? static_cast<std::uint64_t>(*seed) : prj.seed();
    cfg.tickRate = prj.tickRate();
    std::vector<Diagnostic> bd;
    out = PlayWorld::build(prj, worldId, cfg, bd);
    if (!out) {
        Json arr = Json::array(); for (auto& d : bd) arr.push_back(d.toJson());
        fail = Envelope::failure(command, CommandError::make(ErrorCategory::Validation, "WORLD_BUILD_FAILED", "The world could not be built. Run `akeir validate`.", Json{{"diagnostics", arr}}));
        return false;
    }
    game::registerGameSystems(*out);
    return true;
}

#ifdef AKEIR_HAS_SDL

/// SDL 을 dummy driver 로 한 번만 초기화 (capture / test 용: 창 없음)
Platform& headlessSdl() {
    static std::unique_ptr<Platform> p;
    if (!p) {
        PlatformConfig pc; pc.videoDriver = "dummy"; pc.window = false;
        std::string err;
        p = Platform::init(pc, &err);
        if (!p) AKEIR_LOG(Error, "cli", "sdl_init_failed", err);
    }
    return *p;
}

bool captureWorld(const PlayWorld& world, int w, int h, const std::string& outPng, std::string* err, Json* stats = nullptr) {
    headlessSdl();
    auto r = Renderer2D::createSoftware(w, h, err);
    if (!r) return false;
    RenderStats s = r->render(world);
    if (stats) *stats = Json{{"sprites", s.sprites}, {"backend", s.backend}, {"camera", s.camera}};
    std::error_code ec;
    if (std::filesystem::path(outPng).has_parent_path()) std::filesystem::create_directories(std::filesystem::path(outPng).parent_path(), ec);
    return r->savePng(outPng, err);
}

Envelope cmdCapture(Context& ctx) {
    Envelope fail;
    game::registerGameComponents();
    auto prj = openProject(ctx, fail, "capture");
    if (!prj) return fail;
    normalizeFpEnv();
    std::unique_ptr<PlayWorld> world;
    if (!buildWorld(ctx, *prj, "capture", fail, world)) return fail;
    const std::int64_t ticks = ctx.args.getInt("ticks").value_or(0);
    SimTime st; st.tickRate = prj->tickRate();
    for (std::int64_t i = 0; i < ticks; ++i) { InputFrame f; f.tick = st.tick; world->tick(f, st); st.advance(); }
    const int w = static_cast<int>(ctx.args.getInt("width").value_or(512)), h = static_cast<int>(ctx.args.getInt("height").value_or(512));
    std::string out = ctx.args.getOr("out", (std::filesystem::path(prj->rootDir()) / "Cache" / "capture" / ("tick_" + std::to_string(st.tick) + ".png")).string());
    std::string err;
    Json stats;
    if (!captureWorld(*world, w, h, out, &err, &stats)) return Envelope::failure("capture", CommandError::make(ErrorCategory::Internal, "CAPTURE_FAILED", err));
    Json r = Json{{"file", out}, {"width", w}, {"height", h}, {"tick", st.tick}, {"videoDriver", headlessSdl().currentVideoDriver()}, {"renderer", stats.value("backend", "")}, {"sprites", stats.value("sprites", 0)}, {"camera", stats.value("camera", Json())}, {"worldHash", toHex64(world->hash())}};
    if (auto golden = ctx.args.get("compare")) {
        CaptureTolerance tol;
        if (auto v = ctx.args.get("per-pixel")) tol.perPixel = std::atof(v->c_str());
        if (auto v = ctx.args.get("max-mismatch")) tol.maxMismatchRatio = std::atof(v->c_str());
        std::string diff = ctx.args.getOr("diff", "");
        CaptureCompareResult c = compareCaptures(*golden, out, tol, diff);
        r["compare"] = c.toJson();
        r["compare"]["golden"] = *golden;
        if (!diff.empty()) r["compare"]["diff"] = diff;
        if (!c.ok) return Envelope::failure("capture", CommandError::make(ErrorCategory::Validation, "CAPTURE_MISMATCH",
            c.error.empty() ? "Capture differs from golden: " + std::to_string(c.mismatchedPixels) + " px (" + std::to_string(c.ratio * 100) + "%)." : c.error, r));
    }
    return Envelope::success("capture", r);
}

Envelope cmdRunWindowed(Context& ctx) {
    Envelope fail;
    game::registerGameComponents();
    auto prj = openProject(ctx, fail, "run.start");
    if (!prj) return fail;
    normalizeFpEnv();
    PlatformConfig pc;
    pc.width = static_cast<int>(ctx.args.getInt("width").value_or(1280));
    pc.height = static_cast<int>(ctx.args.getInt("height").value_or(720));
    pc.title = prj->name() + " — akeir run";
    pc.videoDriver = ctx.args.getOr("video-driver", "");
    std::string err;
    auto platform = Platform::init(pc, &err);
    if (!platform) return Envelope::failure("run.start", CommandError::make(ErrorCategory::Internal, "SDL_INIT_FAILED", err));
    std::unique_ptr<PlayWorld> world;
    if (!buildWorld(ctx, *prj, "run.start", fail, world)) return fail;
    std::vector<Diagnostic> inputDiags;
    InputMap input = InputMap::loadFile((std::filesystem::path(prj->rootDir()) / "Config" / "input.json").string(), &inputDiags);
    auto renderer = Renderer2D::createForWindow(platform->window(), &err);
    if (!renderer) return Envelope::failure("run.start", CommandError::make(ErrorCategory::Internal, "RENDERER_FAILED", err));
    InteractiveConfig ic;
    ic.maxTicks = ctx.args.getInt("ticks").value_or(0);
    ic.tickRate = prj->tickRate();
    ic.recordInputsPath = ctx.args.getOr("record", "");
    AKEIR_LOG(Info, "runtime", "run_start", "Windowed run starting.", Json{{"videoDriver", platform->currentVideoDriver()}, {"renderer", renderer->backendName()}});
    InteractiveResult ir = runInteractive(*platform, *renderer, *world, input, ic);
    Json r = ir.toJson();
    r["mode"] = "windowed";
    r["videoDriver"] = platform->currentVideoDriver();
    r["renderer"] = renderer->backendName();
    r["world"] = world->worldId();
    r["seed"] = world->seed();
    if (!ic.recordInputsPath.empty()) r["recordedInputs"] = ic.recordInputsPath;
    r["fpFlagsHash"] = AKEIR_FP_FLAGS_HASH;
    Envelope env = Envelope::success("run.start", r);
    for (auto& d : inputDiags) env.withWarning(d);
    return env;
}

Envelope cmdInputMap(Context& ctx) {
    Envelope fail;
    auto prj = openProject(ctx, fail, "input.map");
    if (!prj) return fail;
    headlessSdl();
    std::vector<Diagnostic> diags;
    InputMap m = InputMap::loadFile((std::filesystem::path(prj->rootDir()) / "Config" / "input.json").string(), &diags);
    Envelope env = Envelope::success("input.map", Json{{"actions", m.toJson()}, {"videoDriver", headlessSdl().currentVideoDriver()}});
    for (auto& d : diags) env.withWarning(d);
    return env;
}

#else

Envelope cmdCapture(Context&) { return unavailable("capture"); }
Envelope cmdRunWindowed(Context&) { return unavailable("run.start"); }
Envelope cmdInputMap(Context&) { return unavailable("input.map"); }

#endif

} // namespace

bool sdlAvailable() {
#ifdef AKEIR_HAS_SDL
    return true;
#else
    return false;
#endif
}

Envelope runWindowed(Context& ctx) { return cmdRunWindowed(ctx); }

void installCaptureHooks(TestRunnerOptions& opts) {
#ifdef AKEIR_HAS_SDL
    opts.capture = [](const PlayWorld& w, int width, int height, const std::string& out, std::string* err) { return captureWorld(w, width, height, out, err); };
    opts.compare = [](const std::string& expected, const std::string& actual, const Json& tolJ, const std::string& diffOut) {
        CaptureTolerance tol;
        if (tolJ.is_object()) { tol.perPixel = tolJ.value("perPixel", tol.perPixel); tol.maxMismatchRatio = tolJ.value("maxMismatchRatio", tol.maxMismatchRatio); }
        return compareCaptures(expected, actual, tol, diffOut).toJson();
    };
#else
    (void)opts;
#endif
}

void registerSdlCommands(std::vector<CommandSpec>& t) {
    t.push_back({"capture", {"capture"}, "RuntimeControl", "Render the world to a PNG (§27)",
                 "Software-rasterized capture (no window/GPU; deterministic bytes). --ticks N advances first. --compare golden.png runs the §27.1 comparison (exit 3 on mismatch, --diff writes a diff image).",
                 "akeir capture [--ticks N] [--width W] [--height H] [--out f.png] [--compare golden.png] [--diff d.png] [--per-pixel 0.1] [--max-mismatch 0.002] [--json]", true, false, true, cmdCapture});
    t.push_back({"input.map", {"input", "map"}, "Query", "Show the resolved input action map", "Config/input.json as SDL scancodes; lists unsupported bindings (gamepad/mouse).", "akeir input map [--json]", true, false, true, cmdInputMap});
}

} // namespace akeir::cli
