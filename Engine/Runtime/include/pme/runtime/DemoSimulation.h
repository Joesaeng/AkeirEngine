// pme/runtime/DemoSimulation.h — 결정론 루프 검증용 최소 시뮬레이션 (placeholder).
// 설계 문서 §20.1 / §22.2 의 루프·해시 계약을 Phase 1 World 가 생기기 전에 실측하기 위한 것.
// `akeir run --headless` 는 프로젝트가 로드되지 않았을 때 이것을 돌린다 (result.simulation = "demo").
// Phase 1 이후에는 테스트 전용으로 남는다.
#pragma once

#include "pme/core/Hash.h"
#include "pme/core/Rng.h"
#include "pme/runtime/Application.h"

#include <vector>

namespace pme {

class DemoSimulation : public ISimulation {
public:
    explicit DemoSimulation(std::uint64_t seed, int particleCount = 64)
        : rng_(seed, "DemoSimulation"), spawnRng_(seed, "DemoSimulation.spawn") {
        particles_.reserve(static_cast<std::size_t>(particleCount));
        for (int i = 0; i < particleCount; ++i)
            particles_.push_back({spawnRng_.nextFloat(-10.f, 10.f), spawnRng_.nextFloat(-10.f, 10.f), 0.f, 0.f});
    }

    void tick(const InputFrame& input, const SimTime& t) override {
        const float dt = t.dt();
        const float ax = input.axis("MoveX"), ay = input.axis("MoveY");
        for (auto& p : particles_) {
            // 입력은 0번 particle(플레이어 역할)에만, 나머지는 RNG 로 흔든다
            float jx = rng_.nextFloat(-1.f, 1.f), jy = rng_.nextFloat(-1.f, 1.f);
            p.vx = (&p == &particles_[0]) ? ax * 4.0f : p.vx * 0.98f + jx;
            p.vy = (&p == &particles_[0]) ? ay * 4.0f : p.vy * 0.98f + jy;
            p.x += p.vx * dt;
            p.y += p.vy * dt;
        }
        ++ticks_;
    }

    std::uint64_t hash() const override {
        Hasher h;
        h.i64(ticks_);
        for (const auto& p : particles_) { h.f32(p.x); h.f32(p.y); h.f32(p.vx); h.f32(p.vy); }
        for (auto s : rng_.state()) h.u64(s);
        return h.value();
    }

    Json systemHashes() const override {
        Hasher rngH; for (auto s : rng_.state()) rngH.u64(s);
        Hasher posH; for (const auto& p : particles_) { posH.f32(p.x); posH.f32(p.y); }
        return Json{{"Rng", toHex64(rngH.value())}, {"Movement", toHex64(posH.value())}};
    }

    struct Particle { float x, y, vx, vy; };
    const std::vector<Particle>& particles() const { return particles_; }

private:
    RngStream rng_, spawnRng_;
    std::vector<Particle> particles_;
    std::int64_t ticks_ = 0;
};

} // namespace pme
