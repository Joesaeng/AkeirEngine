// akeir/core/Rng.h — 결정적 난수 스트림. 설계 문서 §22.2 (RNG).
//
//   - 전역 RNG 없음. 시스템별(필요 시 entity별) RngStream.
//   - RngStream = xoshiro256** 을 SplitMix64(hash64(worldSeed, "system.name")) 로 초기화.
//     (PCG/SplitMix 의 "stream" 파라미터로 스트림을 나누지 않는다 — 저자가 "테스트되지 않은 generator" 라고 경고)
//   - 상태는 snapshot 에 들어간다 (§26.1 "rng": { "EnemyAI": [s0,s1,s2,s3] }).
//   - std::mt19937 / rand() / random_device 는 sim 코드에서 금지 (§62 lint).
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace akeir {

class RngStream {
public:
    using State = std::array<std::uint64_t, 4>;

    RngStream() = default;
    explicit RngStream(std::uint64_t seed) { reseed(seed); }
    /// worldSeed 와 스트림 이름에서 파생 (§22.2). 같은 (worldSeed, name) → 같은 수열.
    RngStream(std::uint64_t worldSeed, std::string_view streamName);

    void reseed(std::uint64_t seed);
    void setState(const State& s) { s_ = s; }
    const State& state() const { return s_; }

    std::uint64_t next();                       // xoshiro256**
    double nextDouble();                        // [0, 1)
    float nextFloat();                          // [0, 1)
    std::uint64_t nextRange(std::uint64_t n);   // [0, n), n > 0 (bias-free rejection)
    std::int64_t nextInt(std::int64_t lo, std::int64_t hi); // [lo, hi]
    float nextFloat(float lo, float hi);        // [lo, hi)
    bool nextBool(float probabilityTrue = 0.5f);

    /// 2^128 점프 — 같은 seed 에서 겹치지 않는 부분 수열 (병렬/독립 스트림).
    void jump();

private:
    State s_{};
};

} // namespace akeir
