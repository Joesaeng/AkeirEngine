// akeir/core/Time.h — SimTime 과 WallTime 을 타입으로 분리한다. 설계 문서 §22.2 (Time).
//
//   - Simulation 은 고정 tick 만 쓴다. dt = 1/tickRate 를 매 tick 같은 float 상수로 (누적 합산 금지).
//   - sim 코드는 wall-clock API(SDL_GetTicks, steady_clock …)를 호출하지 않는다. SimTime 만 받는다.
//   - WallTime 은 로그 timestamp(§28), 프로파일(§65), watchdog(§88.4) 등 sim 밖에서만 쓴다.
#pragma once

#include <chrono>
#include <cstdint>

namespace akeir {

/// 시뮬레이션 시간 — tick 카운터가 유일한 진실. 초 단위 값은 파생값이다.
struct SimTime {
    std::int64_t tick = 0;
    std::int32_t tickRate = 60;        // project.json (정수)

    float dt() const { return 1.0f / static_cast<float>(tickRate); }           // 매 tick 같은 상수
    double seconds() const { return static_cast<double>(tick) / tickRate; }   // 표시용 파생값
    void advance() { ++tick; }
};

/// 벽시계 시간 — sim 밖 전용. ns since Unix epoch (OTel Timestamp, §28).
struct WallTime {
    std::uint64_t unixNanos = 0;

    static WallTime now() {
        using namespace std::chrono;
        return {static_cast<std::uint64_t>(duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count())};
    }
    /// ISO-8601 UTC "2026-08-21T10:00:00.123Z"
    std::string iso8601() const;
};

/// 단조 시계 (경과 시간 측정, durationMs). sim 밖 전용.
struct Stopwatch {
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    double elapsedMs() const {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }
};

} // namespace akeir
