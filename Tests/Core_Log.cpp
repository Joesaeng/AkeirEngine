// Core_Log.cpp — 설계 문서 §28 (OTel Logs Data Model, JSONL)
#include <doctest/doctest.h>
#include "akeir/core/Log.h"

#include <string>

using namespace akeir;

TEST_CASE("Log: record shape follows OTel field set (§28)") {
    Logger log;
    auto ring = std::make_shared<RingSink>(8);
    log.addSink(ring);
    log.setCurrentTick(813);
    log.setRunId("run_01j5xq8z3mf0n9k2c7p4rtvw6y");
    log.log(LogLevel::Warn, "Navigation", "nav.target_invalid", "Target entity no longer exists.",
            Json{{"game.entity", "entity_01j5xq8z3mf0n9k2c7p4rtvw6y"}});
    auto recs = ring->snapshot();
    REQUIRE(recs.size() == 1);
    Json j = recs[0].toJson();
    CHECK(j["sev"] == 13);
    CHECK(j["level"] == "WARN");
    CHECK(j["event"] == "nav.target_invalid");
    CHECK(j["scope"] == "Navigation");
    CHECK(j["attrs"]["game.tick"] == 813);
    CHECK(j["attrs"]["game.run_id"] == "run_01j5xq8z3mf0n9k2c7p4rtvw6y");
    CHECK(j["attrs"]["game.entity"] == "entity_01j5xq8z3mf0n9k2c7p4rtvw6y");
    CHECK(j["ts"].get<std::uint64_t>() > 1700000000000000000ULL);
    CHECK(j.begin().key() == "ts"); // ordered_json keeps our field order
}

TEST_CASE("Log: min level filters and ring keeps last N") {
    Logger log;
    auto ring = std::make_shared<RingSink>(3);
    log.addSink(ring);
    log.setMinLevel(LogLevel::Info);
    log.log(LogLevel::Debug, "x", "x.debug", "dropped");
    for (int i = 0; i < 5; ++i) log.log(LogLevel::Info, "x", "x.i", std::to_string(i));
    auto recs = ring->snapshot();
    REQUIRE(recs.size() == 3);
    CHECK(recs[0].body == "2");
    CHECK(recs[2].body == "4");
}
