// Runtime_Application.cpp — 설계 문서 §20.1 (fixed-tick headless 루프), §22.1 T0 (run-twice 동일 hash), §88.3 (InputFrame)
#include <doctest/doctest.h>
#include "akeir/runtime/Application.h"
#include "akeir/runtime/DemoSimulation.h"
#include "akeir/core/FpEnv.h"

using namespace akeir;

TEST_CASE("Application: run twice with same seed and inputs gives identical hash sequence (T0, §22.1)") {
    RunConfig cfg; cfg.ticks = 300; cfg.seed = 381251; cfg.hashEvery = 10;
    DemoSimulation a(cfg.seed), b(cfg.seed);
    NullInputSource in;
    RunResult ra = Application::runHeadless(cfg, a, in);
    RunResult rb = Application::runHeadless(cfg, b, in);
    REQUIRE(ra.hashes.size() == rb.hashes.size());
    CHECK(ra.hashes.size() == 30);
    for (std::size_t i = 0; i < ra.hashes.size(); ++i) {
        CHECK(ra.hashes[i].tick == rb.hashes[i].tick);
        CHECK(ra.hashes[i].world == rb.hashes[i].world);
        CHECK(ra.hashes[i].systems == rb.hashes[i].systems);
    }
    CHECK(ra.finalHash == rb.finalHash);
    CHECK(ra.ticksRun == 300);
}

TEST_CASE("Application: different seed or different input diverges") {
    RunConfig cfg; cfg.ticks = 120; cfg.seed = 1; cfg.hashEvery = 1;
    DemoSimulation a(1), b(2), c(1);
    NullInputSource none;
    ScriptedInputSource scripted;
    InputFrame f; f.tick = 5; f.actions["MoveX"] = 1.0f; scripted.add(f);
    RunResult ra = Application::runHeadless(cfg, a, none);
    RunResult rb = Application::runHeadless(cfg, b, none);
    RunResult rc = Application::runHeadless(cfg, c, scripted);
    CHECK(ra.finalHash != rb.finalHash);
    CHECK(ra.finalHash != rc.finalHash);
    // 입력이 들어간 tick 전까지는 같고 그 이후에 갈라진다 (first divergent tick = 5)
    CHECK(ra.hashes[4].world == rc.hashes[4].world);
    CHECK(ra.hashes[5].world != rc.hashes[5].world);
}

TEST_CASE("Application: hashEvery 0 disables per-tick hashes but still reports finalHash") {
    RunConfig cfg; cfg.ticks = 10; cfg.seed = 7; cfg.hashEvery = 0;
    DemoSimulation a(7);
    NullInputSource in;
    RunResult r = Application::runHeadless(cfg, a, in);
    CHECK(r.hashes.empty());
    CHECK(r.finalHash == a.hash());
    Json j = r.toJson(true);
    CHECK(j["ticksRun"] == 10);
    CHECK(j["finalHash"].get<std::string>().rfind("0x", 0) == 0);
}

TEST_CASE("Application: wantsExit stops the loop early") {
    struct ExitAt3 : ISimulation {
        int n = 0;
        void tick(const InputFrame&, const SimTime&) override { ++n; }
        std::uint64_t hash() const override { return static_cast<std::uint64_t>(n); }
        bool wantsExit() const override { return n >= 3; }
    } sim;
    RunConfig cfg; cfg.ticks = 100;
    NullInputSource in;
    RunResult r = Application::runHeadless(cfg, sim, in);
    CHECK(r.ticksRun == 3);
    CHECK(r.exitedEarly);
}

TEST_CASE("InputFrame: JSON round-trip (replay inputs.jsonl line, §22.3)") {
    InputFrame f; f.tick = 42; f.actions["MoveX"] = -1.0f; f.actions["Attack"] = 1.0f;
    f.commands.push_back(Json{{"op", "property.set"}, {"args", Json{{"path", "/components/Health/max"}, {"value", 5}}}});
    Json j = f.toJson();
    CHECK(j["tick"] == 42);
    CHECK(j["actions"]["MoveX"] == -1.0f);
    InputFrame back = InputFrame::fromJson(j);
    CHECK(back.tick == 42);
    CHECK(back.axis("MoveX") == -1.0f);
    CHECK(back.pressed("Attack"));
    CHECK_FALSE(back.pressed("Jump"));
    REQUIRE(back.commands.size() == 1);
    CHECK(back.commands[0]["op"] == "property.set");
}

TEST_CASE("FpEnv: normalize yields round-to-nearest without FTZ/DAZ (§22.2)") {
    FpEnvStatus s = normalizeFpEnv();
    CHECK(s.roundToNearest);
    CHECK_FALSE(s.flushToZero);
    CHECK_FALSE(s.denormalsAreZero);
    CHECK(s.ok());
    CHECK(fpEnvStatus().toJson()["ok"] == true);
}

TEST_CASE("InputFrame: edges come from withEdges only; pointer and edges round-trip through JSON for replay (ADR-0045)") {
    InputFrame a; a.tick = 0; a.actions["Attack"] = 1.f; a.actions["MoveX"] = 0.3f;
    a.pointer.present = true; a.pointer.x = 100; a.pointer.y = 50; a.pointer.viewportW = 1280; a.pointer.viewportH = 720; a.pointer.buttons = kPointerLeft;
    InputFrame f0 = InputFrame::withEdges(a, nullptr);
    CHECK(f0.justPressed("Attack"));
    CHECK_FALSE(f0.justPressed("MoveX"));   // 0.3 is not "active" (> 0.5)
    CHECK(f0.pointer.justPressed(kPointerLeft));
    CHECK(f0.pointer.nx() == doctest::Approx(100.f / 1280.f));

    InputFrame b; b.tick = 1; b.actions["Attack"] = 1.f; b.pointer = a.pointer; b.pointer.buttons = kPointerLeft | kPointerRight;
    InputFrame f1 = InputFrame::withEdges(b, &f0);
    CHECK_FALSE(f1.justPressed("Attack"));   // still held
    CHECK(f1.held("Attack"));
    CHECK(f1.pointer.pressed == kPointerRight);
    CHECK(f1.pointer.released == 0);

    InputFrame c; c.tick = 2; c.pointer = b.pointer; c.pointer.inside = false;   // focus lost: everything releases
    InputFrame f2 = InputFrame::withEdges(c, &f1);
    CHECK(f2.justReleased("Attack"));
    CHECK(f2.pointer.buttons == 0);
    CHECK(f2.pointer.released == (kPointerLeft | kPointerRight));

    Json j = f1.toJson();
    CHECK(j["pointer"]["buttons"] == Json::array({"left", "right"}));
    CHECK(j["pointer"]["pressed"] == Json::array({"right"}));
    CHECK(j["pointer"]["viewport"] == Json::array({1280, 720}));
    CHECK_FALSE(j.contains("pressed"));
    InputFrame back = InputFrame::fromJson(j);
    CHECK(back.pointer.buttons == f1.pointer.buttons);
    CHECK(back.pointer.pressed == kPointerRight);
    CHECK(back.pointer.x == 100);
    CHECK(back.held("Attack"));
    InputFrame noPtr = InputFrame::fromJson(Json{{"tick", 3}, {"actions", Json::object()}});
    CHECK_FALSE(noPtr.pointer.present);
    CHECK(pointerButtonMask("middle") == kPointerMiddle);
    CHECK(pointerButtonMask("wat") == 0);
}
