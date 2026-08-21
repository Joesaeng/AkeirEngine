// akeir/core/Crash.h — 크래시 / 행 진단. 설계 문서 §88.4, §13 (exit 6 crash / 7 timeout), §63.
//
//   - Windows: SetUnhandledExceptionFilter + MiniDumpWriteDump → <dumpDir>/<stem>.dmp
//     크래시 시 stdout 에 §13 envelope ({"ok":false,"error":{"ruleId":"CRASH","category":"crash",
//     "details":{"minidump":"…","exceptionCode":"0x…"}}}) 를 쓰고 exit 6.
//   - Watchdog: --timeout 초과 시 envelope(ruleId "TIMEOUT", category "timeout") + exit 7.
//   - 마지막 N개 로그(RingSink)는 크래시 envelope 의 details.lastLogs 에 실린다.
//   - 비-Windows 에서는 dump 없이 envelope 만 쓴다.
#pragma once

#include "akeir/core/Json.h"
#include "akeir/core/Log.h"

#include <chrono>
#include <memory>
#include <string>

namespace akeir {

struct CrashConfig {
    std::string dumpDir = "Cache/crash";   // 프로젝트 상대 또는 절대
    std::string stem = "game";             // <stem>-<timestamp>.dmp
    std::shared_ptr<RingSink> lastLogs;    // 선택: 마지막 N개 로그를 envelope 에 싣는다
    std::string command;                   // envelope.command 에 기록할 현재 command id
};

/// 프로세스 전역 크래시 핸들러 설치. 한 번만 호출.
void installCrashHandler(const CrashConfig& cfg);

/// watchdog 시작. 기한 내에 stopWatchdog() 이 불리지 않으면 TIMEOUT envelope + exit 7.
void startWatchdog(std::chrono::milliseconds timeout, const std::string& command);
void stopWatchdog();

/// 테스트/디버그용: 실제 access violation 을 일으킨다 (§74 Phase 0 성공 기준 "강제 crash 시 …").
[[noreturn]] void debugForceCrash();

/// 크래시/타임아웃 envelope 을 만든다 (단위 테스트에서 형태 검증용).
Json makeCrashEnvelope(const std::string& command, const std::string& ruleId, const std::string& category,
                       const std::string& text, Json details);

} // namespace akeir
