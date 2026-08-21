// pme/runtime/Application.h — fixed-tick 실행 루프. 설계 문서 §20.1 (headless 루프는 accumulator 가 없다), §22.2 (Time / Verification), §88.1 (CommandApply 단계).
//
//   headless:
//     for (tick = 0; tick < N; ++tick) {
//         InputFrame in = replay ? replay.Read(tick) : inputs.Drain(tick);   // CLI command 포함 (§8.2)
//         sim.tick(in, simTime);                                              // physics.Step 포함. wall-clock 접근 없음
//         if (tick % hashEvery == 0) hashes.push({tick, sim.hash()});
//     }
//   실시간(창 있는) 모드의 accumulator/interpolation 은 render 쪽 문제이며 Phase 2 에서 SDL 과 함께 붙는다.
//   결정론 계약: 같은 (sim 코드, 데이터, seed, 입력 시퀀스) → 같은 hash 시퀀스 (T0/T1, §22.1).
#pragma once

#include "pme/core/Json.h"
#include "pme/core/Time.h"
#include "pme/runtime/Input.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace pme {

/// sim 이 구현하는 인터페이스. Game/ 의 World 나 테스트용 더미가 이것을 구현한다.
class ISimulation {
public:
    virtual ~ISimulation() = default;
    /// 한 tick 진행. dt 는 simTime.dt() (매 tick 같은 상수). wall-clock 금지.
    virtual void tick(const InputFrame& input, const SimTime& simTime) = 0;
    /// 결정적 world hash (§22.2 Verification). float 은 bit pattern 으로.
    virtual std::uint64_t hash() const = 0;
    /// 선택: 시스템별 sub-hash ("Physics", "EnemyAI", "Rng" …) — 첫 divergent system 을 찾는 데 쓴다 (§24).
    virtual Json systemHashes() const { return Json::object(); }
    /// 선택: sim 이 스스로 종료를 원하면 true (예: 테스트 시나리오의 AppExit 이벤트).
    virtual bool wantsExit() const { return false; }
};

/// 입력 공급자. headless 에서는 replay 파일 또는 테스트 시나리오의 inputs 블록, 실시간에서는 device → action 변환기.
class IInputSource {
public:
    virtual ~IInputSource() = default;
    virtual InputFrame drain(std::int64_t tick) = 0;
};

/// 입력 없음 (모든 tick 에 빈 InputFrame).
class NullInputSource : public IInputSource {
public:
    InputFrame drain(std::int64_t tick) override { InputFrame f; f.tick = tick; return f; }
};

/// 미리 만들어진 프레임 목록 (테스트 시나리오 inputs / replay inputs.jsonl). 없는 tick 은 빈 프레임.
class ScriptedInputSource : public IInputSource {
public:
    void add(InputFrame f) { frames_[f.tick] = std::move(f); }
    InputFrame drain(std::int64_t tick) override;
private:
    std::map<std::int64_t, InputFrame> frames_;
};

struct RunConfig {
    std::int64_t ticks = 60;        // 실행할 tick 수 (--ticks; --frames 는 alias)
    std::int32_t tickRate = 60;     // project.json (정수)
    std::uint64_t seed = 0;         // world seed (--seed)
    std::int64_t hashEvery = 1;     // 0 = 해시 안 함 (--hash-every)
    bool keepHashes = true;         // hashes[] 를 결과에 보관 (대량 tick 이면 파일로)
    std::string videoDriver = "dummy"; // 기록용 (§20: 실제 driver 는 platform layer 가 고른다)
};

struct TickHash {
    std::int64_t tick;
    std::uint64_t world;
    Json systems;   // systemHashes() 결과 (비어 있을 수 있음)
};

struct RunResult {
    std::int64_t ticksRun = 0;
    std::uint64_t seed = 0;
    std::int32_t tickRate = 60;
    std::uint64_t finalHash = 0;
    std::vector<TickHash> hashes;
    double wallMs = 0;              // sim 밖 측정값 (결정론과 무관)
    bool exitedEarly = false;       // sim.wantsExit()

    Json toJson(bool includeHashes) const;
};

class Application {
public:
    /// headless 고정 tick 루프. 블로킹. 매 tick Logger 의 currentTick 을 갱신한다 (§28 attrs.game.tick).
    static RunResult runHeadless(const RunConfig& cfg, ISimulation& sim, IInputSource& input);
};

} // namespace pme
