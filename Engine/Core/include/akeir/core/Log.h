// akeir/core/Log.h — 구조화 로그. 설계 문서 §28 (OpenTelemetry Logs Data Model 필드, JSON Lines).
//
//   한 줄 = 한 record:
//   {"ts":<uint64 ns>,"sev":13,"level":"WARN","event":"nav.target_invalid","body":"…","scope":"Navigation",
//    "attrs":{"game.tick":813,"game.entity":"entity_…", …}}
//
//   - ts   : OTel Timestamp (ns since epoch, WallTime). sim 로그의 순서 기준은 attrs["game.tick"].
//   - sev  : OTel SeverityNumber (TRACE 1–4, DEBUG 5–8, INFO 9–12, WARN 13–16, ERROR 17–20, FATAL 21–24)
//   - event: 안정적 기계 코드 "<scope>.<snake_case>"
//   - 게임 전용 키는 전부 "game." 네임스페이스 아래 (attrs).
//   - stdout 에는 절대 쓰지 않는다 (stdout = envelope, §12). 기본 sink 는 stderr; 파일 sink 추가 가능.
//   - MCP Logging 에는 싣지 않는다 (deprecated, §1).
#pragma once

#include "akeir/core/Json.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace akeir {

enum class LogLevel : int { Trace = 1, Debug = 5, Info = 9, Warn = 13, Error = 17, Fatal = 21 };
const char* logLevelName(LogLevel lvl);

struct LogRecord {
    std::uint64_t ts = 0;          // ns since epoch
    LogLevel level = LogLevel::Info;
    std::string event;             // "<scope>.<snake_case>"
    std::string body;
    std::string scope;
    Json attrs = Json::object();   // "game.tick", "game.entity", …
    Json toJson() const;
};

class LogSink {
public:
    virtual ~LogSink() = default;
    virtual void write(const LogRecord& r) = 0;
    virtual void flush() {}
};

class Logger {
public:
    static Logger& global();

    void addSink(std::shared_ptr<LogSink> sink);
    void clearSinks();
    void setMinLevel(LogLevel lvl) { minLevel_ = lvl; }
    LogLevel minLevel() const { return minLevel_; }

    /// 현재 sim tick 을 attrs["game.tick"] 에 자동으로 싣는다 (Application 이 매 tick 갱신). -1 = 없음.
    void setCurrentTick(std::int64_t tick) { currentTick_ = tick; }
    void setRunId(std::string runId) { runId_ = std::move(runId); }

    void log(LogLevel lvl, std::string_view scope, std::string_view event, std::string_view body, Json attrs = Json::object());
    void flush();

private:
    std::mutex mtx_;
    std::vector<std::shared_ptr<LogSink>> sinks_;
    LogLevel minLevel_ = LogLevel::Info;
    std::int64_t currentTick_ = -1;
    std::string runId_;
};

/// stderr JSONL sink (기본).
std::shared_ptr<LogSink> makeStderrSink();
/// 파일 JSONL sink (append). §24 artifacts log.jsonl.
std::shared_ptr<LogSink> makeFileSink(const std::string& path);
/// 메모리 ring buffer sink — 크래시 시 마지막 N개 flush (§88.4), 테스트 검증용.
class RingSink : public LogSink {
public:
    explicit RingSink(std::size_t capacity = 256) : cap_(capacity) {}
    void write(const LogRecord& r) override;
    std::vector<LogRecord> snapshot() const;
private:
    mutable std::mutex mtx_;
    std::size_t cap_;
    std::vector<LogRecord> ring_;
    std::size_t next_ = 0;
    bool full_ = false;
};

// 편의 매크로 — scope 는 모듈 이름, event 는 "<scope>.<name>" 으로 자동 조합
#define AKEIR_LOG(level, scope, name, body, ...) \
    ::akeir::Logger::global().log(::akeir::LogLevel::level, scope, std::string(scope) + "." + name, body __VA_OPT__(,) __VA_ARGS__)

} // namespace akeir
