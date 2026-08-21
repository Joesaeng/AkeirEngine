// Game/Source/GameComponents.cpp — reflection 등록. 설계 문서 §42.2, §43.1
#include "GameComponents.h"
#include "GameSystems.h"
#include "pme/reflection/Reflect.h"

PME_REFLECT_ENUM(game::AiState, "idle", "chase", "attack", "dead")

namespace game {

PME_REFLECT_BEGIN(Health, "Hit points and death state")
    PME_LIFECYCLE("OnSpawn", "after EnemyAttack", "OnDespawn");
    PME_PROP(max, "Maximum hit points").min(1).unit("hp").ui(1, 1000, 1).required();
    PME_PROP(current, "Current hit points").runtimeOnly().readOnly().save().unit("hp");
PME_REFLECT_END(Health)

PME_REFLECT_BEGIN(Movement, "Locomotion parameters")
    PME_REQUIRES("RigidBody2D");
    PME_PROP(speed, "Top speed").range(0, 50).warn(0.5, 20).unit("m/s");
PME_REFLECT_END(Movement)

PME_REFLECT_BEGIN(PlayerController, "Marks the entity driven by player input (MoveX/MoveY)")
    PME_REQUIRES("Movement");
    PME_LIFECYCLE("OnSpawn", "PlayerMovement system (first)", "OnDespawn");
    PME_PROP(inputScale, "Multiplier applied to input axes").range(0, 5);
PME_REFLECT_END(PlayerController)

PME_REFLECT_BEGIN(EnemyAI, "Controls target selection and combat state")
    PME_REQUIRES("Transform");
    PME_REQUIRES("Movement");
    PME_LIFECYCLE("OnSpawn", "EnemyChase system (after PlayerMovement, before physics)", "OnDespawn");
    PME_PROP(behavior, "Behavior preset name");
    PME_PROP(targetTag, "Tag of entities to hunt");
    PME_PROP(detectionRange, "Start chasing within this distance").min(0).unit("m").ui(0, 50, 0.5);
    PME_PROP(attackRange, "Attack within this distance").min(0).unit("m");
    PME_PROP(damage, "Damage per hit").min(0).unit("hp");
    PME_PROP(attackCooldown, "Seconds between attacks").min(0.01).unit("s");
    PME_PROP(state, "Current state").runtimeOnly().readOnly().save();
    PME_PROP(target, "Current target entity").runtimeOnly().readOnly().refType("entity");
    PME_PROP(cooldownLeft, "Seconds until next attack").runtimeOnly().readOnly().save().unit("s");
PME_REFLECT_END(EnemyAI)

void registerGameComponents() {
    (void)pmeReflectInstance_Health;
    (void)pmeReflectInstance_Movement;
    (void)pmeReflectInstance_PlayerController;
    (void)pmeReflectInstance_EnemyAI;
}

} // namespace game
