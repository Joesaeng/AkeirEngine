// akeir/runtime/Application.cpp — 설계 문서 §20.1, §22.2
#include "akeir/runtime/Application.h"
#include "akeir/core/FpEnv.h"
#include "akeir/core/Hash.h"
#include "akeir/core/Log.h"

namespace akeir {

// ---------------------------------------------------------------- InputFrame

Json InputFrame::toJson() const {
    Json j = Json::object();
    j["tick"] = tick;
    Json a = Json::object();
    for (const auto& [k, v] : actions) a[k] = v;
    j["actions"] = a;
    if (!commands.empty()) j["commands"] = commands;
    return j;
}

InputFrame InputFrame::fromJson(const Json& j) {
    InputFrame f;
    f.tick = j.value("tick", 0LL);
    if (j.contains("actions") && j["actions"].is_object())
        for (auto it = j["actions"].begin(); it != j["actions"].end(); ++it)
            if (it.value().is_number()) f.actions[it.key()] = it.value().get<float>();
            else if (it.value().is_boolean()) f.actions[it.key()] = it.value().get<bool>() ? 1.0f : 0.0f;
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
