// akeir/runtime/Application.cpp — 설계 문서 §20.1, §22.2
#include "akeir/runtime/Application.h"
#include "akeir/core/FpEnv.h"
#include "akeir/core/Hash.h"
#include "akeir/core/Log.h"

#include <algorithm>

namespace akeir {

// ---------------------------------------------------------------- InputFrame

std::uint32_t pointerButtonMask(const std::string& name) {
    if (name == "left") return kPointerLeft;
    if (name == "middle") return kPointerMiddle;
    if (name == "right") return kPointerRight;
    if (name == "x1") return kPointerX1;
    if (name == "x2") return kPointerX2;
    return 0;
}

std::vector<std::string> pointerButtonNames(std::uint32_t mask) {
    std::vector<std::string> out;
    if (mask & kPointerLeft) out.push_back("left");
    if (mask & kPointerMiddle) out.push_back("middle");
    if (mask & kPointerRight) out.push_back("right");
    if (mask & kPointerX1) out.push_back("x1");
    if (mask & kPointerX2) out.push_back("x2");
    return out;
}

bool InputFrame::justPressed(const std::string& name) const { return std::binary_search(pressedActions.begin(), pressedActions.end(), name); }
bool InputFrame::justReleased(const std::string& name) const { return std::binary_search(releasedActions.begin(), releasedActions.end(), name); }

InputFrame InputFrame::withEdges(InputFrame cur, const InputFrame* prev) {
    cur.pressedActions.clear();
    cur.releasedActions.clear();
    if (!cur.pointer.inside) cur.pointer.buttons = 0;   // focus/window loss releases everything (ADR-0045)
    for (const auto& [name, v] : cur.actions)
        if (v > 0.5f && !(prev && prev->held(name))) cur.pressedActions.push_back(name);
    if (prev)
        for (const auto& [name, v] : prev->actions)
            if (v > 0.5f && !cur.held(name)) cur.releasedActions.push_back(name);
    std::sort(cur.pressedActions.begin(), cur.pressedActions.end());
    std::sort(cur.releasedActions.begin(), cur.releasedActions.end());
    const std::uint32_t before = prev ? prev->pointer.buttons : 0u;
    cur.pointer.pressed = cur.pointer.buttons & ~before;
    cur.pointer.released = before & ~cur.pointer.buttons;
    return cur;
}

Json InputFrame::toJson() const {
    Json j = Json::object();
    j["tick"] = tick;
    Json a = Json::object();
    for (const auto& [k, v] : actions) a[k] = v;
    j["actions"] = a;
    if (!pressedActions.empty()) j["pressed"] = pressedActions;
    if (!releasedActions.empty()) j["released"] = releasedActions;
    if (pointer.present) {
        Json p = Json{{"x", pointer.x}, {"y", pointer.y}, {"viewport", Json::array({pointer.viewportW, pointer.viewportH})}};
        if (pointer.buttons) p["buttons"] = pointerButtonNames(pointer.buttons);
        if (pointer.pressed) p["pressed"] = pointerButtonNames(pointer.pressed);
        if (pointer.released) p["released"] = pointerButtonNames(pointer.released);
        if (pointer.wheel != 0.f) p["wheel"] = pointer.wheel;
        if (!pointer.inside) p["inside"] = false;
        j["pointer"] = p;
    }
    if (!commands.empty()) j["commands"] = commands;
    return j;
}

namespace {
std::uint32_t maskOfNames(const Json& arr) {
    std::uint32_t m = 0;
    if (arr.is_array()) for (const auto& n : arr) if (n.is_string()) m |= pointerButtonMask(n.get<std::string>());
    return m;
}
} // namespace

InputFrame InputFrame::fromJson(const Json& j) {
    InputFrame f;
    f.tick = j.value("tick", 0LL);
    if (j.contains("actions") && j["actions"].is_object())
        for (auto it = j["actions"].begin(); it != j["actions"].end(); ++it)
            if (it.value().is_number()) f.actions[it.key()] = it.value().get<float>();
            else if (it.value().is_boolean()) f.actions[it.key()] = it.value().get<bool>() ? 1.0f : 0.0f;
    if (j.contains("pressed") && j["pressed"].is_array()) for (const auto& n : j["pressed"]) if (n.is_string()) f.pressedActions.push_back(n.get<std::string>());
    if (j.contains("released") && j["released"].is_array()) for (const auto& n : j["released"]) if (n.is_string()) f.releasedActions.push_back(n.get<std::string>());
    std::sort(f.pressedActions.begin(), f.pressedActions.end());
    std::sort(f.releasedActions.begin(), f.releasedActions.end());
    if (j.contains("pointer") && j["pointer"].is_object()) {
        const Json& p = j["pointer"];
        f.pointer.present = true;
        f.pointer.x = p.value("x", 0.f);
        f.pointer.y = p.value("y", 0.f);
        if (p.contains("viewport") && p["viewport"].is_array() && p["viewport"].size() == 2) { f.pointer.viewportW = p["viewport"][0].get<int>(); f.pointer.viewportH = p["viewport"][1].get<int>(); }
        f.pointer.buttons = maskOfNames(p.value("buttons", Json::array()));
        f.pointer.pressed = maskOfNames(p.value("pressed", Json::array()));
        f.pointer.released = maskOfNames(p.value("released", Json::array()));
        f.pointer.wheel = p.value("wheel", 0.f);
        f.pointer.inside = p.value("inside", true);
    }
    if (j.contains("commands") && j["commands"].is_array())
        for (const auto& c : j["commands"]) f.commands.push_back(c);
    return f;
}

InputFrame ScriptedInputSource::drain(std::int64_t tick) {
    auto it = frames_.find(tick);
    if (it != frames_.end()) return it->second;
    InputFrame f;
    f.tick = tick;
    return f;
}

// ---------------------------------------------------------------- RunResult

Json RunResult::toJson(bool includeHashes) const {
    Json j = Json::object();
    j["ticksRun"] = ticksRun;
    j["seed"] = seed;
    j["tickRate"] = tickRate;
    j["finalHash"] = toHex64(finalHash);
    j["exitedEarly"] = exitedEarly;
    j["wallMs"] = wallMs;
    if (includeHashes) {
        Json arr = Json::array();
        for (const auto& h : hashes) {
            Json e = Json{{"tick", h.tick}, {"world", toHex64(h.world)}};
            if (!h.systems.empty()) e["systems"] = h.systems;
            arr.push_back(e);
        }
        j["hashes"] = arr;
    } else {
        j["hashCount"] = hashes.size();
    }
    return j;
}

// ---------------------------------------------------------------- Application

RunResult Application::runHeadless(const RunConfig& cfg, ISimulation& sim, IInputSource& input) {
    // §22.2: 스레드 시작 시 FP 환경을 정책에 맞춘다 (round-to-nearest, FTZ/DAZ 끔)
    FpEnvStatus fp = normalizeFpEnv();
    if (!fp.ok())
        AKEIR_LOG(Warn, "runtime", "fpenv_not_normalized", "FP environment could not be normalized; determinism (T0/T1) is at risk.", fp.toJson());

    Stopwatch sw;
    RunResult result;
    result.seed = cfg.seed;
    result.tickRate = cfg.tickRate;

    SimTime simTime;
    simTime.tick = 0;
    simTime.tickRate = cfg.tickRate;

    auto& logger = Logger::global();
    const std::int64_t hashEvery = cfg.hashEvery;

    for (std::int64_t i = 0; i < cfg.ticks; ++i) {
        logger.setCurrentTick(simTime.tick);
        InputFrame in = input.drain(simTime.tick);            // CommandApply 단계: 이 tick 의 입력·command 가 여기서 고정된다 (§8.2)
        sim.tick(in, simTime);
        if (hashEvery > 0 && (simTime.tick % hashEvery) == 0) {
            TickHash th{simTime.tick, sim.hash(), sim.systemHashes()};
            result.finalHash = th.world;
            if (cfg.keepHashes) result.hashes.push_back(std::move(th));
        }
        simTime.advance();
        result.ticksRun = simTime.tick;
        if (sim.wantsExit()) { result.exitedEarly = true; break; }
    }
    if (hashEvery <= 0 || result.ticksRun == 0 || ((result.ticksRun - 1) % hashEvery) != 0) result.finalHash = sim.hash();
    logger.setCurrentTick(-1);
    result.wallMs = sw.elapsedMs();
    return result;
}

} // namespace akeir
