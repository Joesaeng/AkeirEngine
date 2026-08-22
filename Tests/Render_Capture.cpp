// Render_Capture.cpp — 설계 문서 §27 (software capture 는 결정적), §27.1 (golden 비교). AKEIR_WITH_SDL 빌드에서만 컴파일된다.
#include <doctest/doctest.h>
#include "GameSystems.h"
#include "akeir/platform/InputMap.h"
#include "akeir/platform/Platform.h"
#include "akeir/render/Renderer2D.h"
#include "akeir/runtime/Components.h"

#include <filesystem>
#include <fstream>

using namespace akeir;
namespace fs = std::filesystem;

namespace {
std::string sampleDir() {
    // frozen fixture (Tests/Fixtures/TestArena, ADR-0036) — never the user's Game/, which may be any game
    return std::string(AKEIR_TEST_FIXTURES) + "/TestArena";
}
Platform& sdl() {
    static std::unique_ptr<Platform> p;
    if (!p) { PlatformConfig c; c.videoDriver = "dummy"; c.window = false; std::string err; p = Platform::init(c, &err); REQUIRE_MESSAGE(p, err); }
    return *p;
}
std::string readBytes(const fs::path& p) { std::ifstream in(p, std::ios::binary); return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()); }
} // namespace

TEST_CASE("Renderer2D: software renderer works without a window — two captures of the same world are byte-identical (§27)") {
    sdl();
    CHECK(sdl().currentVideoDriver() == "dummy");
    registerBuiltinComponents();
    game::registerGameComponents();
    std::vector<Diagnostic> d;
    auto prj = Project::load(sampleDir(), d);
    REQUIRE(prj);
    PlayWorldConfig cfg; cfg.seed = prj->seed(); cfg.tickRate = prj->tickRate();
    auto world = PlayWorld::build(*prj, *prj->defaultWorld(), cfg, d);
    REQUIRE(world);
    game::registerGameSystems(*world);
    SimTime st; st.tickRate = cfg.tickRate;
    for (int i = 0; i < 120; ++i) { InputFrame f; f.tick = st.tick; world->tick(f, st); st.advance(); }

    std::string err;
    auto r = Renderer2D::createSoftware(256, 256, &err);
    REQUIRE_MESSAGE(r, err);
    CHECK(r->backendName() == "software");
    RenderStats s = r->render(*world);
    CHECK(s.sprites == 4);                      // Player + 3 goblins (SpriteRenderer)
    CHECK(s.camera["entity"].get<std::string>().rfind("entity_", 0) == 0);   // MainCamera
    int w = 0, h = 0;
    auto px = r->readPixels(&w, &h);
    REQUIRE(px.size() == 256u * 256u * 4u);
    // 배경색이 아닌 픽셀이 있어야 한다 (무언가 그려짐)
    std::size_t nonBg = 0;
    for (std::size_t i = 0; i < px.size(); i += 4) if (!(px[i] == px[4] && px[i + 1] == px[5] && px[i + 2] == px[6])) ++nonBg;
    CHECK(nonBg > 50);

    fs::path dir = fs::temp_directory_path() / "akeir_capture_test";
    std::error_code ec; fs::create_directories(dir, ec);
    REQUIRE(r->savePng((dir / "a.png").string(), &err));
    auto r2 = Renderer2D::createSoftware(256, 256, &err);
    r2->render(*world);
    REQUIRE(r2->savePng((dir / "b.png").string(), &err));
    CHECK(readBytes(dir / "a.png") == readBytes(dir / "b.png"));

    CaptureCompareResult same = compareCaptures((dir / "a.png").string(), (dir / "b.png").string());
    CHECK(same.ok);
    CHECK(same.mismatchedPixels == 0);
    // 60 tick 더 돌리면 고블린이 움직여 다르다
    for (int i = 0; i < 60; ++i) { InputFrame f; f.tick = st.tick; world->tick(f, st); st.advance(); }
    r2->render(*world);
    REQUIRE(r2->savePng((dir / "c.png").string(), &err));
    CaptureCompareResult diff = compareCaptures((dir / "a.png").string(), (dir / "c.png").string(), {}, (dir / "diff.png").string());
    CHECK_FALSE(diff.ok);
    CHECK(diff.mismatchedPixels > 0);
    CHECK(fs::exists(dir / "diff.png"));
    CaptureCompareResult lenient = compareCaptures((dir / "a.png").string(), (dir / "c.png").string(), CaptureTolerance{0.1, 1.0});
    CHECK(lenient.ok);
    fs::remove_all(dir, ec);
}

TEST_CASE("InputMap: Config/input.json resolves to scancodes — sample() with no keys is empty (§88.3)") {
    sdl();
    std::vector<Diagnostic> diags;
    InputMap m = InputMap::loadFile((fs::path(sampleDir()) / "Config" / "input.json").string(), &diags);
    CHECK(diags.empty());
    REQUIRE(m.actions().size() == 3);
    CHECK(m.actions()[0].name == "Attack");
    CHECK_FALSE(m.actions()[0].axis);
    CHECK(m.actions()[1].name == "MoveX");
    CHECK(m.actions()[1].axis);
    REQUIRE(m.actions()[1].bindings.size() == 2);
    CHECK(m.actions()[1].bindings[0].scales == std::vector<float>{-1.f, 1.f});
    CHECK(m.actions()[1].unsupported == std::vector<std::string>{"gamepad:leftStickX"});
    InputFrame f = m.sample(7);
    CHECK(f.tick == 7);
    CHECK(f.actions.empty());
    Json bad = Json::parse(R"json({"actions":{"Jump":{"type":"button","bindings":[{"key":"NotAKey"}]}}})json");
    InputMap m2 = InputMap::fromJson(bad, &diags);
    REQUIRE(diags.size() == 1);
    CHECK(diags[0].ruleId == "INPUT_KEY_UNKNOWN");
}
