// akeir/core/ExitCodes.h — CLI exit code 표. 설계 문서 §13.
//
//   exit code 는 보조 신호다. envelope 은 항상 error.ruleId 를 들고 있다.
//   `akeir capabilities --json` 의 result.exitCodes 가 이 표를 그대로 노출한다.
#pragma once

namespace akeir {

enum ExitCode : int {
    kExitOk = 0,                 // ok
    kExitCommandFailed = 1,      // 도메인 오류. envelope 은 stdout
    kExitUsage = 2,              // usage / argument error (stderr)
    kExitFindings = 3,           // validation/test failed with findings (akeir validate, akeir test)
    kExitConfirmationRequired = 4, // 파괴적 작업을 --yes 없이 호출. error.details.confirmCommand
    kExitNotFound = 5,           // 모르는 id / handle
    kExitCrash = 6,              // crash (minidump 경로 포함, §88.4)
    kExitTimeout = 7,            // watchdog
    kExitInterrupted = 130,
    kExitTerminated = 143,
};

inline const char* exitCodeDescription(int code) {
    switch (code) {
    case kExitOk: return "ok";
    case kExitCommandFailed: return "command failed (domain error; envelope on stdout)";
    case kExitUsage: return "usage / argument error (stderr)";
    case kExitFindings: return "validation/test failed with findings";
    case kExitConfirmationRequired: return "confirmation required (re-run with --yes / error.details.confirmCommand)";
    case kExitNotFound: return "not found (unknown id / handle)";
    case kExitCrash: return "crash (see error.details.minidump)";
    case kExitTimeout: return "timeout (watchdog)";
    case kExitInterrupted: return "interrupted";
    case kExitTerminated: return "terminated";
    default: return "unknown";
    }
}

} // namespace akeir
