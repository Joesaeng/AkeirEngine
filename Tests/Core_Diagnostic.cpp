// Core_Diagnostic.cpp — 설계 문서 §79 Diagnostic, §13 envelope, §88.4 crash envelope
#include <doctest/doctest.h>
#include "pme/core/Crash.h"
#include "pme/core/Diagnostic.h"
#include "pme/core/ExitCodes.h"

using namespace pme;

TEST_CASE("Diagnostic: JSON shape matches §79 and round-trips") {
    Fix fix;
    fix.description = "Add Transform component";
    fix.applicability = Applicability::MachineApplicable;
    fix.isPreferred = true;
    fix.commands.push_back(CommandInvocation{"component.add", Json{{"entity", "entity_01j5xq8z3mf0n9k2c7p4rtvw6y"}, {"component", "Transform"}}});
    fix.cli = "game component add entity_01j5xq8z3mf0n9k2c7p4rtvw6y Transform --json";

    Diagnostic d = Diagnostic::error("COMPONENT_DEPENDENCY_MISSING", "CharacterMovement requires Transform.")
                       .at(LogicalLocation{"entity_01j5xq8z3mf0n9k2c7p4rtvw6y", std::string("CharacterMovement"), std::nullopt})
                       .in(PhysicalLocation{"Worlds/TestArena.world.json",
                                            "/entities/entity_01j5xq8z3mf0n9k2c7p4rtvw6y/components/CharacterMovement",
                                            Region{42, 5, 48, 6}})
                       .withFix(fix);
    Json j = d.toJson();
    CHECK(j["ruleId"] == "COMPONENT_DEPENDENCY_MISSING");
    CHECK(j["level"] == "error");
    CHECK(j["message"]["text"] == "CharacterMovement requires Transform.");
    CHECK(j["logical"]["component"] == "CharacterMovement");
    CHECK(j["physical"]["jsonPointer"].get<std::string>().rfind("/entities/", 0) == 0);
    CHECK(j["physical"]["region"]["startLine"] == 42);
    CHECK(j["fixes"][0]["applicability"] == "MachineApplicable");
    CHECK(j["fixes"][0]["commands"][0]["op"] == "component.add");
    CHECK(j["fingerprint"].get<std::string>().rfind("0x", 0) == 0);
    CHECK(j["helpUri"] == "game://docs/rules/COMPONENT_DEPENDENCY_MISSING");

    auto back = Diagnostic::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->toJson() == j);

    Diagnostic d2 = d;
    d2.message.text = "other";
    CHECK(d2.computeFingerprint() == d.computeFingerprint()); // fingerprint depends on location, not message
}

TEST_CASE("Diagnostic: summary counts") {
    std::vector<Diagnostic> v{Diagnostic::error("A", ""), Diagnostic::warning("B", ""), Diagnostic::warning("C", ""), Diagnostic::note("D", "")};
    auto s = summarize(v);
    CHECK(s.error == 1);
    CHECK(s.warning == 2);
    CHECK(s.note == 1);
}

TEST_CASE("Crash envelope has §13 shape and exit codes are stable") {
    Json env = makeCrashEnvelope("run.start", "CRASH", "crash", "boom", Json{{"minidump", "Cache/crash/x.dmp"}});
    CHECK(env["ok"] == false);
    CHECK(env["command"] == "run.start");
    CHECK(env["error"]["ruleId"] == "CRASH");
    CHECK(env["error"]["category"] == "crash");
    CHECK(env["error"]["details"]["minidump"] == "Cache/crash/x.dmp");
    CHECK(env.contains("warnings"));
    CHECK(env.contains("changes"));
    CHECK(env.contains("meta"));
    CHECK(kExitCrash == 6);
    CHECK(kExitTimeout == 7);
    CHECK(kExitFindings == 3);
    CHECK(kExitConfirmationRequired == 4);
}
