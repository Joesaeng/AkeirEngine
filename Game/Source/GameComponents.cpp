// Game/Source/GameComponents.cpp — reflection 등록. 설계 문서 §42.2, §43.1
#include "GameComponents.h"
#include "GameSystems.h"
#include "akeir/reflection/Reflect.h"

AKEIR_REFLECT_ENUM(game::AiState, "idle", "chase", "attack", "dead")

namespace game {

AKEIR_REFLECT_BEGIN(Health, "Hit points and death state")
    AKEIR_LIFECYCLE("OnSpawn", "after EnemyAttack", "OnDespawn");
    AKEIR_PROP(max, "Maximum hit points").min(1).unit("hp").ui(1, 1000, 1).required();
    AKEIR_PROP(current, "Current hit points").runtimeOnly().readOnly().save().unit("hp");
AKEIR_REFLECT_END(Health)

AKEIR_REFLECT_BEGIN(Movement, "Locomotion parameters")
    AKEIR_REQUIRES("RigidBody2D");
    AKEIR_PROP(speed, "Top speed").range(0, 50).warn(0.5, 20).unit("m/s");
AKEIR_REFLECT_END(Movement)

AKEIR_REFLECT_BEGIN(PlayerController, "Marks the entity driven by player input (MoveX/MoveY)")
    AKEIR_REQUIRES("Movement");
    AKEIR_LIFECYCLE("OnSpawn", "PlayerMovement system (first)", "OnDespawn");
    AKEIR_PROP(inputScale, "Multiplier applied to input axes").range(0, 5);
AKEIR_REFLECT_END(PlayerController)

AKEIR_REFLECT_BEGIN(EnemyAI, "Controls target selection and combat state")
    AKEIR_REQUIRES("Transform");
    AKEIR_REQUIRES("Movement");
    AKEIR_LIFECYCLE("OnSpawn", "EnemyChase system (after PlayerMovement, before physics)", "OnDespawn");
    AKEIR_PROP(behavior, "Behavior preset name");
    AKEIR_PROP(targetTag, "Tag of entities to hunt");
    AKEIR_PROP(detectionRange, "Start chasing within this distance").min(0).unit("m").ui(0, 50, 0.5);
    AKEIR_PROP(attackRange, "Attack within this distance").min(0).unit("m");
    AKEIR_PROP(damage, "Damage per hit").min(0).unit("hp");
    AKEIR_PROP(attackCooldown, "Seconds between attacks").min(0.01).unit("s");
    AKEIR_PROP(state, "Current state").runtimeOnly().readOnly().save();
    AKEIR_PROP(target, "Current target entity").runtimeOnly().readOnly().refType("entity");
    AKEIR_PROP(cooldownLeft, "Seconds until next attack").runtimeOnly().readOnly().save().unit("s");
AKEIR_REFLECT_END(EnemyAI)

void registerGameComponents() {
    (void)akeirReflectInstance_Health;
    (void)akeirReflectInstance_Movement;
    (void)akeirReflectInstance_PlayerController;
    (void)akeirReflectInstance_EnemyAI;
}

} // namespace game
