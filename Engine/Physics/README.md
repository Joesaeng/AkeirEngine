# Engine/Physics (`akeir_physics`)

`PhysicsWorld` 인터페이스 + Box2D v3.1.1 백엔드 (§57). 2D 전용. Jolt 는 목표 게임이 3D 일 때만 (ADR-0003).

- `createBox2DWorld(PhysicsConfig{gravity, enableSleep, subSteps})`
- `createBody(BodyDesc{handle, type, position, mass, linearDamping, gravityScale, fixedRotation, shape(box/circle/capsule), size/radius/offset, isSensor, layerBits/maskBits})` — handle 은 호출자가 정한 안정 키 (PlayWorld 는 id 순 순번)
- `step(dt, subSteps)` 뒤 `drainContactEvents()` 는 (a, b, kind) 정렬된 이벤트 (§22.2)
- `setVelocity`, `setTransform`, `getState`, `hashInto(Hasher)` handle 순, `allStates()` snapshot 용
- mass 는 density × area 로 맞춘다. capsule 은 세로 방향(길이 = radius).

결정론: Box2D 는 worker 없이(단일 스레드) 돈다. v3.1+ 은 worker 수와 무관하게 bit-exact (FAQ). MSVC `/fp:precise` 는 `AKEIR_FP_FLAGS_OPTIONS` 로 Box2D 타깃에도 걸려 있다 (cmake/Dependencies.cmake). Box2D 에는 SaveState 가 없으므로 snapshot 복원 = world 재생성 (§26.1, §58).
