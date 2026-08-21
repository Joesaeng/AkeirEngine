// pme/core/FpEnv.h — 부동소수점 환경 검사/정규화. 설계 문서 §22.2 (Build flags: "프로세스/worker thread 시작 시 FPU 환경 assert").
//
//   결정론 계약(T0/T1)의 전제: 모든 sim 스레드가 같은 rounding mode(round-to-nearest)와 같은 denormal 정책을 가진다.
//   정책: round-to-nearest, denormals 보존(FTZ/DAZ 끔) — MSVC 기본값이며 Box2D/Jolt 의 기대와 일치.
//   `normalizeFpEnv()` 는 현재 스레드를 이 정책으로 맞추고, `fpEnvStatus()` 는 검사 결과를 JSON 으로 돌려준다
//   (`akeir version --json` / replay header 에 기록).
#pragma once

#include "pme/core/Json.h"

namespace pme {

struct FpEnvStatus {
    bool roundToNearest = false;
    bool flushToZero = false;      // FTZ (결과 denormal → 0)
    bool denormalsAreZero = false; // DAZ (입력 denormal → 0)
    bool ok() const { return roundToNearest && !flushToZero && !denormalsAreZero; }
    Json toJson() const;
};

/// 현재 스레드의 FP 환경을 읽는다.
FpEnvStatus fpEnvStatus();
/// 현재 스레드를 정책(round-to-nearest, FTZ/DAZ 끔)으로 맞춘다. 반환값 = 맞춘 뒤 상태.
FpEnvStatus normalizeFpEnv();

} // namespace pme
