// Game/Source/GameSystems.h — TestArena 의 system. 설계 문서 §59 (좁은 엔진 API: PlayWorld 의 get/query/rng/events 만), §71 (PoC 시나리오), §22.2 (결정론 규칙).
//
//   system 은 PlayWorld::addSystem 으로 등록되며 tick 마다 등록 순서대로 실행된다 (물리 step 전).
//     1. PlayerMovement : MoveX/MoveY 입력 → PlayerController 가 있는 entity 의 RigidBody2D.velocity
//     2. EnemyChase     : EnemyAI 가 targetTag 를 가진 가장 가까운 entity 를 찾아 접근 (detectionRange 안이면 chase, attackRange 안이면 attack)
//     3. EnemyAttack    : attack 상태 + cooldown 0 이면 target 의 Health.current 감소; 0 이하 → dead (RigidBody 정지)
//   규칙: wall-clock·전역 RNG·unordered 순회 금지. 거리 비교에 sqrt 허용 (§22.2).
#pragma once

namespace pme { class PlayWorld; }

namespace game {

/// 컴포넌트 등록 앵커 (정적 라이브러리 링크 보장) — main() 에서 한 번 호출
void registerGameComponents();
/// 위 3개 system 을 순서대로 등록
void registerGameSystems(pme::PlayWorld& world);

} // namespace game
