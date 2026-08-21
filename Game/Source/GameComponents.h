// Game/Source/GameComponents.h — TestArena 게임 component. 설계 문서 §6 (예시 데이터: Health / Movement / EnemyAI), §60 (Game/ 은 AI 가 자주 수정하는 영역), §42.2 (aggregate 규칙).
//
//   엔진 내장 component(Transform, Collider2D, RigidBody2D, SpriteRenderer, Camera2D)는 Engine/Runtime/Components.h 에 있다.
//   여기 있는 것은 이 게임만의 규칙이다. 새 component 를 추가하면 GameComponents.cpp 에 PME_REFLECT_* 로 등록한다.
#pragma once

#include "pme/core/Ref.h"

#include <string>

namespace game {

enum class AiState { Idle, Chase, Attack, Dead };

/// 체력. current 는 runtime 값 (authoring 파일에는 max 만).
struct Health {
    float max = 100.f;
    float current = 100.f;   // runtimeOnly, save
};

/// 이동 능력. 실제 속도는 RigidBody2D.velocity 로 나간다 (PlayerMovement / EnemyChase system).
struct Movement {
    float speed = 4.f;       // m/s
};

/// 플레이어 입력을 받는 entity 표시 (tag 대신 component 로 두면 schema 에 드러난다).
struct PlayerController {
    float inputScale = 1.f;
};

/// 적 AI (§6 예시의 EnemyAI).
struct EnemyAI {
    std::string behavior = "melee_basic";
    std::string targetTag = "player";
    float detectionRange = 10.f;
    float attackRange = 1.0f;
    float damage = 10.f;
    float attackCooldown = 1.0f;
    AiState state = AiState::Idle;   // runtimeOnly
    pme::Ref target;                 // runtimeOnly
    float cooldownLeft = 0.f;        // runtimeOnly
};

} // namespace game
