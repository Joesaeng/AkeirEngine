# Engine/Core (`akeir_core`)

Foundation types with no game logic. Every other Engine module and the Tools depend on it. **Core knows nothing about Game/** (§76).

| Header | Design § | Provides |
|---|---|---|
| `akeir/core/Json.h` | §5.3 | `akeir::Json` (= `nlohmann::ordered_json`), `JsonPointer` |
| `akeir/core/Id.h` | §7.1–7.4 | `Uuid`, `Id` (TypeID), `Id::generate` (v7) / `Id::deterministic` (v8) / `parse` / `validate` / `matchesShortForm`, base32 |
| `akeir/core/Hash.h` | §22.2 §37 §52 | `fnv1a64`, `splitMix64`, `hash64Combine`, `Hasher`, `Sha256`, `toHex`, `toHex64` |
| `akeir/core/Rng.h` | §22.2 | `RngStream` (xoshiro256**) |
| `akeir/core/Time.h` | §22.2 | `SimTime`, `WallTime`, `Stopwatch` |
| `akeir/core/Log.h` | §28 | `Logger`, `LogRecord`, sinks (stderr / file / `RingSink`), `AKEIR_LOG` |
| `akeir/core/Diagnostic.h` | §79 | `Diagnostic`, `Fix`, `Applicability`, `LogicalLocation`, `PhysicalLocation`, `summarize` |
| `akeir/core/Envelope.h` | §12 §13 | `Envelope`, `CommandError`, `ErrorCategory` |
| `akeir/core/ExitCodes.h` | §13 | exit code table |
| `akeir/core/Crash.h` | §88.4 | `installCrashHandler`, `startWatchdog`/`stopWatchdog`, `debugForceCrash`, `makeCrashEnvelope` |

## Usage example

```cpp
#include "akeir/core/Id.h"
#include "akeir/core/Envelope.h"
#include "akeir/core/Log.h"

akeir::Id id = akeir::Id::generate("entity");              // entity_01j5xq8z3mf0n9k2c7p4rtvw6y
auto parsed = akeir::Id::parse("ENTITY_01J5XQ8Z3MF0N9K2C7P4RTVW6Y");   // case is normalized

AKEIR_LOG(Warn, "Navigation", "target_invalid", "Target entity no longer exists.",
        akeir::Json{{"game.entity", id.str()}});          // stderr: {"ts":…,"sev":13,"event":"Navigation.target_invalid",…}

akeir::Envelope env = akeir::Envelope::success("entity.create", akeir::Json{{"id", id.str()}});
std::cout << env.toJson().dump() << "\n";              // §12 envelope
return env.exitCode();                                  // 0
```

## Implemented / not implemented

- Implemented: everything in the table above. Unit tests in `Tests/Core_*.cpp` (19 cases).
- Not implemented: the FPU environment assert (§22.2, remaining Phase 0 item — planned as `FpEnv.h` next to `Time.h`), deterministic trigonometry `det::Sin/Cos` (§22.2 — wrap Box2D's `b2ComputeCosSin` once the Physics module lands).
- The only non-deterministic randomness lives inside `Id::generate` (authoring only). Simulation code uses `RngStream` exclusively.

## Design notes

- `Logger::global()` has a stderr sink by default. Tests create a separate `Logger` instance and verify through a ring sink.
- `Crash` holds process-global state (handler, watchdog thread). Call `installCrashHandler` only once per process.
- `Envelope::toJson()` fills in the meta defaults (schemaVersion, engineVersion, dryRun, truncated, nextCursor) — callers only set `durationMs`, `tx`, etc.
