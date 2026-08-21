// pme/core/Log.cpp — 설계 문서 §28
#include "pme/core/Log.h"
#include "pme/core/Time.h"

#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>

namespace pme {

const char* logLevelName(LogLevel lvl) {
    switch (lvl) {
    case LogLevel::Trace: return "TRACE"; case LogLevel::Debug: return "DEBUG"; case LogLevel::Info: return "INFO";
    case LogLevel::Warn: return "WARN"; case LogLevel::Error: return "ERROR"; default: return "FATAL";
    }
}

std::string WallTime::iso8601() const {
    std::time_t secs = static_cast<std::time_t>(unixNanos / 1000000000ULL);
    unsigned ms = static_cast<unsigned>((unixNanos / 1000000ULL) % 1000ULL);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &secs);
#else
    gmtime_r(&secs, &tm);
#endif
    char buf[40];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d.%03uZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
    return buf;
}

Json LogRecord::toJson() const {
    Json j = Json::object();
    j["ts"] = ts;
    j["sev"] = static_cast<int>(level);
    j["level"] = logLevelName(level);
    j["event"] = event;
    j["body"] = body;
    j["scope"] = scope;
    j["attrs"] = attrs;
    return j;
}

namespace {
class StderrSink : public LogSink {
public:
    void write(const LogRecord& r) override { std::fputs(r.toJson().dump().c_str(), stderr); std::fputc('\n', stderr); }
    void flush() override { std::fflush(stderr); }
};
class FileSink : public LogSink {
public:
    explicit FileSink(const std::string& path) : out_(path, std::ios::app | std::ios::binary) {}
    void write(const LogRecord& r) override { if (out_) out_ << r.toJson().dump() << '\n'; }
    void flush() override { if (out_) out_.flush(); }
private:
    std::ofstream out_;
};
} // namespace

std::shared_ptr<LogSink> makeStderrSink() { return std::make_shared<StderrSink>(); }
std::shared_ptr<LogSink> makeFileSink(const std::string& path) { return std::make_shared<FileSink>(path); }

void RingSink::write(const LogRecord& r) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (ring_.size() < cap_) { ring_.push_back(r); return; }
    ring_[next_] = r; next_ = (next_ + 1) % cap_; full_ = true;
}
std::vector<LogRecord> RingSink::snapshot() const {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!full_) return ring_;
    std::vector<LogRecord> out;
    out.reserve(ring_.size());
    for (std::size_t i = 0; i < ring_.size(); ++i) out.push_back(ring_[(next_ + i) % ring_.size()]);
    return out;
}

Logger& Logger::global() {
    static Logger instance;
    static bool initialized = false;
    if (!initialized) { initialized = true; instance.addSink(makeStderrSink()); }
    return instance;
}

void Logger::addSink(std::shared_ptr<LogSink> sink) { std::lock_guard<std::mutex> lock(mtx_); sinks_.push_back(std::move(sink)); }
void Logger::clearSinks() { std::lock_guard<std::mutex> lock(mtx_); sinks_.clear(); }

void Logger::log(LogLevel lvl, std::string_view scope, std::string_view event, std::string_view body, Json attrs) {
    if (static_cast<int>(lvl) < static_cast<int>(minLevel_)) return;
    LogRecord r;
    r.ts = WallTime::now().unixNanos;
    r.level = lvl;
    r.scope = std::string(scope);
    r.event = std::string(event);
    r.body = std::string(body);
    r.attrs = attrs.is_object() ? std::move(attrs) : Json::object();
    if (currentTick_ >= 0 && !r.attrs.contains("game.tick")) r.attrs["game.tick"] = currentTick_;
    if (!runId_.empty() && !r.attrs.contains("game.run_id")) r.attrs["game.run_id"] = runId_;
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto& s : sinks_) s->write(r);
}

void Logger::flush() { std::lock_guard<std::mutex> lock(mtx_); for (auto& s : sinks_) s->flush(); }

} // namespace pme
