# Engine/Physics (`akeir_physics`)

The `PhysicsWorld` interface + the Box2D v3.1.1 backend (§57). 2D only. Jolt only if the target game goes 3D (ADR-0003).

- `createBox2DWorld(PhysicsConfig{gravity, enableSleep, subSteps})`
- `createBody(BodyDesc{handle, type, position, mass, linearDamping, gravityScale, fixedRotation, shape(box/circle/capsule), size/radius/offset, isSensor, layerBits/maskBits})` — `handle` is a stable key chosen by the caller (PlayWorld uses the ordinal in id order)
- after `step(dt, subSteps)`, `drainContactEvents()` returns events sorted by (a, b, kind) (§22.2)
- `setVelocity`, `setTransform`, `getState`, `hashInto(Hasher)` in handle order, `allStates()` for snapshots
- Mass is matched via density × area. Capsules are vertical (length = radius).

Determinism: Box2D runs without workers (single-threaded). v3.1+ is bit-exact regardless of worker count (FAQ). MSVC `/fp:precise` is applied to the Box2D target too via `AKEIR_FP_FLAGS_OPTIONS` (cmake/Dependencies.cmake). Box2D has no SaveState, so snapshot restore = world re-creation (§26.1, §58).
