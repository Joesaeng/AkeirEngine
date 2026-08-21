// Serialization_ComponentJson.cpp — 설계 문서 §14 (schema 검증), §26.1/§88.8 (visibility), §29 (검증 코드), §79 (fix applicability)
#include <doctest/doctest.h>
#include "akeir/reflection/Reflect.h"
#include "akeir/serialization/ComponentJson.h"

using namespace akeir;

enum class SerMode { Walk, Run };
AKEIR_REFLECT_ENUM(SerMode, "walk", "run")

struct SerMovement {
    float speed = 4.0f;
    SerMode mode = SerMode::Walk;
    Vec2 dir{1.f, 0.f};
    Ref target;
    float stamina = 1.0f;
    int lane = 0;
};
AKEIR_REFLECT_BEGIN(SerMovement, "Movement")
    AKEIR_PROP(speed, "Units per second").range(0, 50).warn(0.5, 20).unit("m/s").required();
    AKEIR_PROP(mode, "Gait");
    AKEIR_PROP(dir, "Facing direction");
    AKEIR_PROP(target, "Chase target").refType("entity");
    AKEIR_PROP(stamina, "0..1").runtimeOnly().save();
    AKEIR_PROP(lane, "Lane index").range(0, 3).multipleOf(1);
AKEIR_REFLECT_END(SerMovement)

static const ComponentMeta& meta() { return *Registry::global().find("SerMovement"); }

TEST_CASE("ComponentJson: toJson honors visibility and canonical floats (§26.1, §5.3)") {
    SerMovement m; m.speed = 4.8f; m.stamina = 0.3f;
    Json a = componentToJson(meta(), &m, Visibility::Authoring);
    CHECK(a["speed"] == 4.8);                 // 0.30000001192092896 같은 값이 아니다
    CHECK(a["dir"] == Json::array({1.0, 0.0}));
    CHECK(a["mode"] == "walk");
    CHECK(a["target"] == "");
    CHECK_FALSE(a.contains("stamina"));       // runtimeOnly
    Json s = componentToJson(meta(), &m, Visibility::Snapshot);
    CHECK(s["stamina"] == 0.3);
    Json sv = componentToJson(meta(), &m, Visibility::Save);
    CHECK(sv.contains("stamina"));
    CHECK_FALSE(sv.contains("speed"));
    auto it = a.begin();
    CHECK(it.key() == "speed");               // 선언 순서
}

TEST_CASE("ComponentJson: validation produces §79 diagnostics with fixes") {
    PhysicalLocation where{"Prefabs/Goblin.prefab.json", "/components/SerMovement", std::nullopt};
    const std::string eid = "prefab_01j5xq8z3mf0n9k2c7p4rtvw6y";

    SUBCASE("valid") {
        Json j = Json{{"speed", 5.0}, {"mode", "run"}, {"dir", Json::array({0, 1})}, {"target", "entity_01j5xq8z3mf0n9k2c7p4rtvw6y"}, {"lane", 2}};
        auto d = validateComponentJson(meta(), j, where, Visibility::Authoring, eid);
        CHECK(d.empty());
        SerMovement m;
        auto r = componentFromJson(meta(), &m, j, Visibility::Authoring, where, eid);
        CHECK(r.ok);
        CHECK(m.speed == 5.0f); CHECK(m.mode == SerMode::Run); CHECK(m.dir == Vec2{0, 1}); CHECK(m.lane == 2);
        CHECK(m.target.idPart() == "entity_01j5xq8z3mf0n9k2c7p4rtvw6y");
    }
    SUBCASE("unknown property → error with remove fix") {
        Json j = Json{{"speed", 5.0}, {"velocity", 3}};
        auto d = validateComponentJson(meta(), j, where, Visibility::Authoring, eid);
        REQUIRE(d.size() == 1);
        CHECK(d[0].ruleId == "PROPERTY_UNKNOWN");
        CHECK(d[0].physical->jsonPointer == "/components/SerMovement/velocity");
        CHECK(d[0].logical->object == eid);
        CHECK(d[0].fixes[0].artifactChanges[0]["op"] == "remove");
    }
    SUBCASE("type mismatch / enum / ref / range / multipleOf / required") {
        Json j = Json{{"speed", "fast"}, {"mode", "fly"}, {"dir", Json::array({1, 2, 3})}, {"target", "bogus"}, {"lane", 7}};
        auto d = validateComponentJson(meta(), j, where, Visibility::Authoring, eid);
        auto find = [&](const char* rule) -> const Diagnostic* { for (const auto& x : d) if (x.ruleId == rule) return &x; return nullptr; };
        REQUIRE(find("PROPERTY_TYPE_MISMATCH"));                       // speed, dir
        REQUIRE(find("ENUM_VALUE_INVALID"));
        CHECK(find("ENUM_VALUE_INVALID")->fixes.size() == 2);           // walk / run 제안
        CHECK(find("ENUM_VALUE_INVALID")->fixes[0].applicability == Applicability::MaybeIncorrect);
        REQUIRE(find("REF_FORMAT_INVALID"));
        REQUIRE(find("PROPERTY_OUT_OF_RANGE"));
        CHECK(find("PROPERTY_OUT_OF_RANGE")->fixes[0].applicability == Applicability::MachineApplicable);
        CHECK(find("PROPERTY_OUT_OF_RANGE")->fixes[0].commands[0].args["value"] == 3);   // clamp to max
        CHECK(find("PROPERTY_OUT_OF_RANGE")->fixes[0].commands[0].op == "property.set");
        SerMovement m;
        auto r = componentFromJson(meta(), &m, j, Visibility::Authoring, where, eid);
        CHECK_FALSE(r.ok);
        CHECK(m.lane == 0);                                              // all-or-nothing: 적용 안 됨
    }
    SUBCASE("warn range is a warning, not an error; runtimeOnly in authoring is a warning") {
        Json j = Json{{"speed", 30.0}, {"stamina", 0.5}};
        auto d = validateComponentJson(meta(), j, where, Visibility::Authoring, eid);
        REQUIRE(d.size() == 2);
        CHECK(d[0].ruleId == "PROPERTY_OUT_OF_WARN_RANGE");
        CHECK(d[0].level == Severity::Warning);
        CHECK(d[1].ruleId == "RUNTIME_ONLY_IN_AUTHORING");
        CHECK(d[1].fixes[0].applicability == Applicability::MachineApplicable);
        SerMovement m;
        auto r = componentFromJson(meta(), &m, j, Visibility::Authoring, where, eid);
        CHECK(r.ok);
        CHECK(m.speed == 30.0f);
        CHECK(m.stamina == 1.0f);                                        // runtimeOnly 는 authoring 적용에서 무시
        auto r2 = componentFromJson(meta(), &m, Json{{"stamina", 0.5}}, Visibility::Snapshot);
        CHECK(r2.ok);
        CHECK(m.stamina == 0.5f);
    }
    SUBCASE("required missing") {
        auto d = validateComponentJson(meta(), Json::object(), where, Visibility::Authoring, eid);
        REQUIRE(d.size() == 1);
        CHECK(d[0].ruleId == "PROPERTY_REQUIRED_MISSING");
        CHECK(d[0].fixes[0].commands[0].args["value"] == 4.0);
    }
}
