// Render_Capture.cpp — 설계 문서 §27 (software capture 는 결정적), §27.1 (golden 비교). AKEIR_WITH_SDL 빌드에서만 컴파일된다.
#include <doctest/doctest.h>
#include "GameSystems.h"
#include "TestPng.h"
#include "akeir/core/Id.h"
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

TEST_CASE("Renderer2D: a SpriteRenderer.sprite asset ref draws the texture region with nearest filtering, flips and pivot (ADR-0037)") {
    sdl();
    registerBuiltinComponents();
    fs::path dir = fs::temp_directory_path() / "akeir_sprite_render_test";
    std::error_code ec; fs::remove_all(dir, ec);
    fs::create_directories(dir / "Assets"); fs::create_directories(dir / "Worlds");
    // 2x2 texture: red green / blue white
    std::vector<std::uint8_t> px = {255, 0, 0, 255,  0, 255, 0, 255,  0, 0, 255, 255,  255, 255, 255, 255};
    REQUIRE(akeirtest::writePng((dir / "Assets" / "t.png").string(), 2, 2, px));
    std::string aid = Id::generate("asset").str();
    std::ofstream(dir / "Assets" / "t.png.meta.json") << Json{{"$schema", "game://schema/asset-meta/1"}, {"schemaVersion", 1}, {"id", aid}, {"source", "Assets/t.png"}, {"importer", "Texture2D"},
        {"settings", Json{{"filter", "nearest"}, {"pixelsPerUnit", 2}}}, {"subAssets", Json::array({Json{{"name", "q"}, {"kind", "sprite"}, {"rect", Json::array({0, 0, 2, 2})}}})}}.dump();
    std::ofstream(dir / "project.json") << Json{{"$schema", "game://schema/project/1"}, {"schemaVersion", 1}, {"name", "R"}, {"tickRate", 60}, {"seed", 1}, {"defaultWorld", "world_01j5xq8z3mf0n9k2c7p4rtvw6y"}}.dump();
    auto worldDoc = [&](bool flipX) {
        return Json{{"$schema", "game://schema/world/1"}, {"schemaVersion", 1}, {"id", "world_01j5xq8z3mf0n9k2c7p4rtvw6y"}, {"name", "W"}, {"entities", Json{
            {"entity_01j5xq8z3mf0n9k2c7p4rtvw70", Json{{"name", "Cam"}, {"components", Json{{"Transform", Json::object()}, {"Camera2D", Json{{"orthoSize", 1}, {"background", Json::array({0, 0, 0, 1})}}}}}}},
            {"entity_01j5xq8z3mf0n9k2c7p4rtvw71", Json{{"name", "S"}, {"components", Json{{"Transform", Json::object()}, {"SpriteRenderer", Json{{"sprite", aid + "#sprites/q"}, {"flipX", flipX}}}}}}}}}};
    };
    auto renderPixels = [&](bool flipX, int& w, int& h) {
        std::ofstream(dir / "Worlds" / "W.world.json") << worldDoc(flipX).dump();
        std::vector<Diagnostic> d;
        auto prj = Project::load(dir.string(), d);
        REQUIRE(prj);
        for (const auto& x : prj->validate()) REQUIRE(x.ruleId == "JSON_NOT_CANONICAL");
        PlayWorldConfig cfg; cfg.seed = 1; cfg.tickRate = 60;
        auto world = PlayWorld::build(*prj, *prj->defaultWorld(), cfg, d);
        REQUIRE(world);
        std::string err;
        auto r = Renderer2D::createSoftware(64, 64, &err);
        REQUIRE_MESSAGE(r, err);
        RenderStats s = r->render(*world);
        CHECK(s.sprites == 1);
        return r->readPixels(&w, &h);
    };
    // orthoSize 1 on 64px → 32 px per unit; the 2x2 texture at ppu 2 is 1x1 unit = 32x32 px centered: quadrants of 16x16
    int w = 0, h = 0;
    auto a = renderPixels(false, w, h);
    REQUIRE(a.size() == 64u * 64u * 4u);
    auto at = [&](const std::vector<std::uint8_t>& v, int x, int y) { std::size_t i = (static_cast<std::size_t>(y) * 64 + x) * 4; return std::array<int, 3>{v[i], v[i + 1], v[i + 2]}; };
    CHECK(at(a, 24, 24) == std::array<int, 3>{255, 0, 0});     // top-left quadrant = red (texture row 0 is the top)
    CHECK(at(a, 40, 24) == std::array<int, 3>{0, 255, 0});     // top-right = green
    CHECK(at(a, 24, 40) == std::array<int, 3>{0, 0, 255});     // bottom-left = blue
    CHECK(at(a, 40, 40) == std::array<int, 3>{255, 255, 255}); // bottom-right = white
    CHECK(at(a, 4, 4) == std::array<int, 3>{0, 0, 0});         // background untouched
    CHECK(at(a, 31, 31) == std::array<int, 3>{255, 0, 0});     // nearest: no blending at the quadrant edge
    auto b = renderPixels(true, w, h);
    CHECK(at(b, 24, 24) == std::array<int, 3>{0, 255, 0});     // flipX swaps the columns
    CHECK(at(b, 40, 40) == std::array<int, 3>{0, 0, 255});
    fs::remove_all(dir, ec);
}

TEST_CASE("Renderer2D: TextRenderer draws the 5x7 bitmap font in screen space, deterministic pixels (ADR-0040)") {
    sdl();
    registerBuiltinComponents();
    fs::path dir = fs::temp_directory_path() / "akeir_text_render_test";
    std::error_code ec; fs::remove_all(dir, ec);
    fs::create_directories(dir / "Worlds");
    std::ofstream(dir / "project.json") << Json{{"$schema", "game://schema/project/1"}, {"schemaVersion", 1}, {"name", "T"}, {"tickRate", 60}, {"seed", 1}, {"defaultWorld", "world_01j5xq8z3mf0n9k2c7p4rtvw6y"}}.dump();
    std::ofstream(dir / "Worlds" / "W.world.json") << Json{{"$schema", "game://schema/world/1"}, {"schemaVersion", 1}, {"id", "world_01j5xq8z3mf0n9k2c7p4rtvw6y"}, {"name", "W"}, {"entities", Json{
        {"entity_01j5xq8z3mf0n9k2c7p4rtvw70", Json{{"name", "Cam"}, {"components", Json{{"Transform", Json::object()}, {"Camera2D", Json{{"background", Json::array({0, 0, 0, 1})}}}}}}},
        {"entity_01j5xq8z3mf0n9k2c7p4rtvw71", Json{{"name", "Hud"}, {"components", Json{{"Transform", Json{{"position", Json::array({2, 3, 0})}}},
            {"TextRenderer", Json{{"text", "Ia"}, {"screenSpace", true}, {"scale", 1}, {"color", Json::array({1, 1, 1, 1})}}}}}}}}}}.dump();
    std::vector<Diagnostic> d;
    auto prj = Project::load(dir.string(), d);
    REQUIRE(prj);
    PlayWorldConfig cfg; cfg.seed = 1; cfg.tickRate = 60;
    auto world = PlayWorld::build(*prj, *prj->defaultWorld(), cfg, d);
    REQUIRE(world);
    std::string err;
    auto r = Renderer2D::createSoftware(32, 16, &err);
    REQUIRE_MESSAGE(r, err);
    RenderStats s = r->render(*world);
    CHECK(s.texts == 1);
    int w = 0, h = 0;
    auto px = r->readPixels(&w, &h);
    auto on = [&](int x, int y) { std::size_t i = (static_cast<std::size_t>(y) * 32 + x) * 4; return px[i] == 255 && px[i + 1] == 255 && px[i + 2] == 255; };
    // 'I' at (2,3): row 0 = " ### " → x 3..5 lit, x 2 and 6 dark; row 3 = "  #  " → only x 4
    CHECK(on(3, 3)); CHECK(on(4, 3)); CHECK(on(5, 3)); CHECK_FALSE(on(2, 3)); CHECK_FALSE(on(6, 3));
    CHECK(on(4, 6)); CHECK_FALSE(on(3, 6));
    // 'a' is drawn as 'A' (advance 6): row 0 " ### " at x 9..11
    CHECK(on(9, 3)); CHECK(on(11, 3)); CHECK_FALSE(on(8, 3));
    CHECK_FALSE(on(0, 0));
    auto r2 = Renderer2D::createSoftware(32, 16, &err); r2->render(*world);
    CHECK(r2->readPixels(&w, &h) == px);   // deterministic
    fs::remove_all(dir, ec);
}


TEST_CASE("Renderer2D: TextRenderer.font draws a TTF through the glyph atlas (Korean included), falls back to the bitmap font when unresolvable (ADR-0046)") {
    sdl();
    registerBuiltinComponents();
    // a system font: the repo ships none (licenses); CI's windows runner and any Windows box have these. Skip otherwise.
    fs::path fontFile;
    for (const char* cand : {"C:/Windows/Fonts/malgun.ttf", "C:/Windows/Fonts/arial.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"}) if (fs::exists(cand)) { fontFile = cand; break; }
    if (fontFile.empty()) { MESSAGE("no system TTF found; skipping"); return; }
    const bool korean = fontFile.filename() == "malgun.ttf";
    fs::path dir = fs::temp_directory_path() / "akeir_font_render_test";
    std::error_code ec; fs::remove_all(dir, ec);
    fs::create_directories(dir / "Worlds"); fs::create_directories(dir / "Assets" / "Fonts");
    fs::copy_file(fontFile, dir / "Assets" / "Fonts" / "ui.ttf", fs::copy_options::overwrite_existing);
    std::ofstream(dir / "Assets" / "Fonts" / "ui.ttf.meta.json") << Json{{"$schema", "game://schema/asset-meta/1"}, {"schemaVersion", 1}, {"id", "asset_01j5xq8z3mf0n9k2c7p4rtvw90"}, {"source", "Assets/Fonts/ui.ttf"}, {"importer", "Font"}, {"importerVersion", 1}, {"settings", Json::object()}}.dump();
    std::ofstream(dir / "project.json") << Json{{"$schema", "game://schema/project/1"}, {"schemaVersion", 1}, {"name", "F"}, {"tickRate", 60}, {"seed", 1}, {"defaultWorld", "world_01j5xq8z3mf0n9k2c7p4rtvw6y"}}.dump();
    const std::string text = korean ? "\xED\x95\x9C\xEA\xB8\x80 HP" : "Hello HP";   // "한글 HP"
    std::ofstream(dir / "Worlds" / "W.world.json") << Json{{"$schema", "game://schema/world/1"}, {"schemaVersion", 1}, {"id", "world_01j5xq8z3mf0n9k2c7p4rtvw6y"}, {"name", "W"}, {"entities", Json{
        {"entity_01j5xq8z3mf0n9k2c7p4rtvw70", Json{{"name", "Cam"}, {"components", Json{{"Transform", Json::object()}, {"Camera2D", Json{{"background", Json::array({0, 0, 0, 1})}}}}}}},
        {"entity_01j5xq8z3mf0n9k2c7p4rtvw71", Json{{"name", "Hud"}, {"components", Json{{"Transform", Json{{"position", Json::array({4, 4, 0})}}},
            {"TextRenderer", Json{{"text", text}, {"screenSpace", true}, {"font", "asset_01j5xq8z3mf0n9k2c7p4rtvw90"}, {"size", 24}, {"color", Json::array({1, 1, 1, 1})}}}}}}},
        {"entity_01j5xq8z3mf0n9k2c7p4rtvw72", Json{{"name", "Bad"}, {"components", Json{{"Transform", Json{{"position", Json::array({4, 60, 0})}}},
            {"TextRenderer", Json{{"text", "X"}, {"screenSpace", true}, {"scale", 1}, {"font", "asset_01j5xq8z3mf0n9k2c7p4rtvw91"}, {"color", Json::array({1, 1, 1, 1})}}}}}}}}}}.dump();
    std::vector<Diagnostic> d;
    auto prj = Project::load(dir.string(), d);
    REQUIRE(prj);
    CHECK(prj->assets().resolveFont(Ref{"asset_01j5xq8z3mf0n9k2c7p4rtvw90"}) != nullptr);
    std::string why;
    CHECK(prj->assets().resolveFont(Ref{"asset_01j5xq8z3mf0n9k2c7p4rtvw90#sprites/x"}, &why) == nullptr);
    CHECK(why.find("whole") != std::string::npos);
    auto v = prj->validate();
    bool dangling = false;
    for (const auto& x : v) if (x.ruleId == "REF_DANGLING") dangling = true;
    CHECK(dangling);   // the Bad entity's font id has no sidecar
    PlayWorldConfig cfg; cfg.seed = 1; cfg.tickRate = 60;
    auto world = PlayWorld::build(*prj, *prj->defaultWorld(), cfg, d);
    REQUIRE(world);
    std::string err;
    auto r = Renderer2D::createSoftware(160, 80, &err);
    REQUIRE_MESSAGE(r, err);
    RenderStats s = r->render(*world);
    CHECK(s.texts == 2);
    CHECK(s.glyphs >= (korean ? 4 : 6));   // spaces draw nothing
    int w = 0, h = 0;
    auto px = r->readPixels(&w, &h);
    auto lit = [&](int x0, int y0, int x1, int y1) { int n = 0; for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) { std::size_t i = (static_cast<std::size_t>(y) * 160 + x) * 4; if (px[i] > 64) ++n; } return n; };
    CHECK(lit(4, 4, 120, 34) > 40);        // glyph pixels inside the text box (24 px line at y 4..~32)
    CHECK(lit(0, 0, 4, 80) == 0);          // nothing left of the pen
    CHECK(lit(4, 60, 10, 67) > 3);         // 'X' from the bitmap fallback at (4,60)
    auto r2 = Renderer2D::createSoftware(160, 80, &err); r2->render(*world);
    CHECK(r2->readPixels(&w, &h) == px);   // deterministic across renderers (fresh atlases)
    RenderStats s2 = r->render(*world);     // same renderer: atlas reused, same pixels
    CHECK(s2.glyphs == s.glyphs);
    CHECK(r->readPixels(&w, &h) == px);
    fs::remove_all(dir, ec);
}
