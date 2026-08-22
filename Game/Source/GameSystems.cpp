// Game/Source/GameSystems.cpp — 설계 문서 §59, §71, §22.2
#include "GameSystems.h"
#include "GameComponents.h"
#include "akeir/core/Log.h"
#include "akeir/ecs/PlayWorld.h"
#include "akeir/runtime/Components.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace game {

using namespace akeir;

namespace {

float dist2(const Vec3& a, const Vec3& b) { float dx = a.x - b.x, dy = a.y - b.y; return dx * dx + dy * dy; }

void playerMovement(PlayWorld& w, const InputFrame& in, const SimTime&) {
    const float mx = in.axis("MoveX"), my = in.axis("MoveY");
    for (const auto& id : w.query({"PlayerController", "Movement", "RigidBody2D"})) {
        auto* pc = w.get<PlayerController>(id);
        auto* mv = w.get<Movement>(id);
        auto* rb = w.get<RigidBody2D>(id);
        float len = std::sqrt(mx * mx + my * my);
        float nx = len > 1.f ? mx / len : mx, ny = len > 1.f ? my / len : my;   // 대각선 정규화
        rb->velocity = {nx * mv->speed * pc->inputScale, ny * mv->speed * pc->inputScale};
    }
}

void enemyChase(PlayWorld& w, const InputFrame&, const SimTime&) {
    for (const auto& id : w.query({"EnemyAI", "Movement", "RigidBody2D", "Transform"})) {
        auto* ai = w.get<EnemyAI>(id);
        auto* mv = w.get<Movement>(id);
        auto* rb = w.get<RigidBody2D>(id);
        auto* tf = w.get<Transform>(id);
        if (ai->state == AiState::Dead) { rb->velocity = {0.f, 0.f}; continue; }
        // 가장 가까운 target (id 순 순회 → 동률이면 id 가 작은 쪽: 결정적)
        std::string best;
        float bestD2 = std::numeric_limits<float>::max();
        for (const auto& cand : w.query({"#" + ai->targetTag, "Transform"})) {
            if (cand == id) continue;
            if (const auto* hp = w.get<Health>(cand); hp && hp->current <= 0.f) continue;
            float d2 = dist2(tf->position, w.get<Transform>(cand)->position);
            if (d2 < bestD2) { bestD2 = d2; best = cand; }
        }
        float d = std::sqrt(bestD2);
        if (best.empty() || d > ai->detectionRange) { ai->state = AiState::Idle; ai->target.value.clear(); rb->velocity = {0.f, 0.f}; continue; }
        ai->target.value = best;
        const Vec3& tp = w.get<Transform>(best)->position;
        if (d <= ai->attackRange) { ai->state = AiState::Attack; rb->velocity = {0.f, 0.f}; continue; }
        ai->state = AiState::Chase;
        float dx = tp.x - tf->position.x, dy = tp.y - tf->position.y;
        float inv = d > 1e-6f ? 1.f / d : 0.f;
        rb->velocity = {dx * inv * mv->speed, dy * inv * mv->speed};
    }
}

void enemyAttack(PlayWorld& w, const InputFrame&, const SimTime& t) {
    const float dt = t.dt();
    for (const auto& id : w.query({"EnemyAI"})) {
        auto* ai = w.get<EnemyAI>(id);
        if (ai->state == AiState::Dead) continue;
        ai->cooldownLeft = std::max(0.f, ai->cooldownLeft - dt);
        if (ai->state != AiState::Attack || ai->cooldownLeft > 0.f || ai->target.empty()) continue;
        auto* hp = w.get<Health>(ai->target.value);
        if (!hp || hp->current <= 0.f) continue;
        hp->current = std::max(0.f, hp->current - ai->damage);
        ai->cooldownLeft = ai->attackCooldown;
        AKEIR_LOG(Info, "game", "damage", "Enemy hit target.", Json{{"game.entity", id}, {"game.target", ai->target.value}, {"game.damage", ai->damage}, {"game.target_hp", hp->current}});
    }
    // 죽음 처리: Health 0 이하인 EnemyAI → dead
    for (const auto& id : w.query({"EnemyAI", "Health"})) {
        auto* ai = w.get<EnemyAI>(id);
        const auto* hp = w.get<Health>(id);
        if (ai->state != AiState::Dead && hp->current <= 0.f) { ai->state = AiState::Dead; AKEIR_LOG(Info, "game", "enemy_dead", "Enemy died.", Json{{"game.entity", id}}); }
    }
}

// HUD (ADR-0040): rewrite the text of every #hud TextRenderer after physics so it shows this tick's state.
// Uppercase-only bitmap font: "HP 100  GOBLINS 3".
void hudText(PlayWorld& w, const InputFrame&, const SimTime&) {
    const Health* hp = nullptr;
    auto players = w.query({"#player"});
    if (!players.empty()) hp = w.get<Health>(players.front());
    int alive = 0;
    for (const auto& id : w.query({"EnemyAI"})) if (w.get<EnemyAI>(id)->state != AiState::Dead) ++alive;
    std::string text = "HP " + std::to_string(hp ? static_cast<int>(std::lround(hp->current)) : 0) + "  GOBLINS " + std::to_string(alive);
    for (const auto& id : w.query({"TextRenderer", "#hud"})) w.get<TextRenderer>(id)->text = text;
}

} // namespace

void registerGameSystems(PlayWorld& world) {
    world.addSpawnHook("HealthInit", [](PlayWorld& w, const std::string& id) {
        if (auto* hp = w.get<Health>(id)) hp->current = hp->max;    // runtimeOnly 초기화 (§18 lifecycle init)
    });
    world.addSystem("PlayerMovement", playerMovement);
    world.addSystem("EnemyChase", enemyChase);
    world.addSystem("EnemyAttack", enemyAttack);
    world.addSystem("HudText", hudText, PlayWorld::SystemPhase::PostPhysics);   // sees the damage dealt this tick
}

} // namespace game
