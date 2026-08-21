# Engine/Core (`akeir_core`)

게임 로직과 무관한 기반 타입. 다른 모든 Engine 모듈과 Tools 가 의존한다. **Core 는 Game/ 을 모른다** (§76).

| 헤더 | 설계 § | 제공 |
|---|---|---|
| `akeir/core/Json.h` | §5.3 | `akeir::Json` (= `nlohmann::ordered_json`), `JsonPointer` |
| `akeir/core/Id.h` | §7.1–7.4 | `Uuid`, `Id` (TypeID), `Id::generate`(v7) / `Id::deterministic`(v8) / `parse` / `validate` / `matchesShortForm`, base32 |
| `akeir/core/Hash.h` | §22.2 §37 §52 | `fnv1a64`, `splitMix64`, `hash64Combine`, `Hasher`, `Sha256`, `toHex`, `toHex64` |
| `akeir/core/Rng.h` | §22.2 | `RngStream` (xoshiro256**) |
| `akeir/core/Time.h` | §22.2 | `SimTime`, `WallTime`, `Stopwatch` |
| `akeir/core/Log.h` | §28 | `Logger`, `LogRecord`, sinks (stderr / file / `RingSink`), `AKEIR_LOG` |
| `akeir/core/Diagnostic.h` | §79 | `Diagnostic`, `Fix`, `Applicability`, `LogicalLocation`, `PhysicalLocation`, `summarize` |
| `akeir/core/Envelope.h` | §12 §13 | `Envelope`, `CommandError`, `ErrorCategory` |
| `akeir/core/ExitCodes.h` | §13 | exit code 표 |
| `akeir/core/Crash.h` | §88.4 | `installCrashHandler`, `startWatchdog`/`stopWatchdog`, `debugForceCrash`, `makeCrashEnvelope` |

## 사용 예

```cpp
#include "akeir/core/Id.h"
#include "akeir/core/Envelope.h"
#include "akeir/core/Log.h"

akeir::Id id = akeir::Id::generate("entity");              // entity_01j5xq8z3mf0n9k2c7p4rtvw6y
auto parsed = akeir::Id::parse("ENTITY_01J5XQ8Z3MF0N9K2C7P4RTVW6Y");   // 대소문자 정규화

AKEIR_LOG(Warn, "Navigation", "target_invalid", "Target entity no longer exists.",
        akeir::Json{{"game.entity", id.str()}});          // stderr: {"ts":…,"sev":13,"event":"Navigation.target_invalid",…}

akeir::Envelope env = akeir::Envelope::success("entity.create", akeir::Json{{"id", id.str()}});
std::cout << env.toJson().dump() << "\n";              // §12 envelope
return env.exitCode();                                  // 0
```

## 구현 범위 / 미구현

- 구현됨: 위 표 전부. 단위 테스트 `Tests/Core_*.cpp` (19 케이스).
- 미구현: FPU 환경 assert(§22.2, Phase 0 남은 항목 — `Time.h` 옆에 `FpEnv.h` 로 예정), `det::Sin/Cos` 결정적 삼각함수(§22.2 — Physics 모듈 도입 시 Box2D `b2ComputeCosSin` 래핑).
- 비결정적 난수는 `Id::generate` 내부(authoring 전용)에만 있다. sim 코드는 `RngStream` 만 쓴다.

## 설계상 주의

- `Logger::global()` 은 기본으로 stderr sink 를 가진다. 테스트에서는 별도 `Logger` 인스턴스를 만들어 ring sink 로 검증한다.
- `Crash` 는 프로세스 전역 상태(핸들러, watchdog 스레드)를 가진다. 한 프로세스에서 `installCrashHandler` 는 한 번만.
- `Envelope::toJson()` 이 meta 기본값(schemaVersion, engineVersion, dryRun, truncated, nextCursor)을 채운다 — 호출자는 `durationMs`, `tx` 등만 넣으면 된다.
