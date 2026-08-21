// Reflection_Registry.cpp — 설계 문서 §42.2 (PropertyMeta / registry), §14 (JSON Schema 2020-12), §14.1 (wire_format), §43.1 (속성 어휘), §88.8 (visibility)
#include <doctest/doctest.h>
#include "akeir/reflection/Reflect.h"

using namespace akeir;

// ---- 테스트용 component (aggregate) ----
enum class TestShape { Box, Sphere, Capsule };
AKEIR_REFLECT_ENUM(TestShape, "box", "sphere", "capsule")

struct TestHealth {
    float max = 100.f;
    float current = 100.f;
    bool invulnerable = false;
};
AKEIR_REFLECT_BEGIN(TestHealth, "Hit points and death state")
    AKEIR_REQUIRES("Transform");
    AKEIR_LIFECYCLE("OnSpawn", "after EnemyAI", "OnDespawn");
    AKEIR_PROP(max, "Maximum hit points").min(1).unit("hp").ui(1, 1000, 1).required();
    AKEIR_PROP(current, "Current hit points").runtimeOnly().readOnly().save();
    AKEIR_PROP(invulnerable, "Ignore damage").advanced();
AKEIR_REFLECT_END(TestHealth)

struct TestCollider {
    TestShape shape = TestShape::Capsule;
    float radius = 0.4f;
    Vec3 offset{};
    Ref material;
    std::string layer = "Default";
    int priority = 0;
};
AKEIR_REFLECT_BEGIN(TestCollider, "Collision shape")
    AKEIR_PROP(shape, "Primitive shape");
    AKEIR_PROP(radius, "Radius in meters").min(0.01).warn(0.05, 10.0).unit("m");
    AKEIR_PROP(offset, "Local offset");
    AKEIR_PROP(material, "Physics material asset").refType("asset:material");
    AKEIR_PROP(layer, "Collision layer name");
    AKEIR_PROP(priority, "Resolution priority").range(-10, 10);
AKEIR_REFLECT_END(TestCollider)

TEST_CASE("Reflection: static registration puts components in the global registry (§42.2)") {
    const ComponentMeta* h = Registry::global().find("TestHealth");
    REQUIRE(h != nullptr);
    CHECK(h == Registry::global().find<TestHealth>());
    CHECK(h->size == sizeof(TestHealth));
    CHECK(h->requiresComponents == std::vector<std::string>{"Transform"});
    CHECK(h->lifecycle["init"] == "OnSpawn");
    REQUIRE(h->props.size() == 3);
    CHECK(h->props[0].name == "max");
    CHECK(h->props[0].type == PropType::Float);
    CHECK(h->props[0].minimum == 1.0);
    CHECK(h->props[0].unit == "hp");
    CHECK(h->props[0].defaultValue == 100.0);          // default 는 aggregate 초기값에서 읽는다
    CHECK(has(h->props[0].flags, PropFlags::Required));
    CHECK(has(h->props[1].flags, PropFlags::RuntimeOnly));
    CHECK(has(h->props[1].flags, PropFlags::ReadOnly));
    CHECK(has(h->props[1].flags, PropFlags::Save));
    // 이름순 결정적 순서
    auto all = Registry::global().all();
    CHECK(all.size() >= 2);
    for (std::size_t i = 1; i < all.size(); ++i) CHECK(all[i - 1]->name < all[i]->name);
}

TEST_CASE("Reflection: get/set via accessors and JSON pointers, leaf arrays by index (§14.1)") {
    const ComponentMeta* c = Registry::global().find("TestCollider");
    REQUIRE(c);
    TestCollider col;
    CHECK(*getByPointer(*c, &col, "/shape") == "capsule");
    CHECK(*getByPointer(*c, &col, "/offset") == Json::array({0.0, 0.0, 0.0}));
    CHECK(*getByPointer(*c, &col, "/offset/2") == 0.0);
    CHECK_FALSE(getByPointer(*c, &col, "/offset/x").has_value());   // /x 는 없다 — index 만
    CHECK_FALSE(getByPointer(*c, &col, "/offset/3").has_value());
    CHECK_FALSE(getByPointer(*c, &col, "offset").has_value());      // pointer 는 / 로 시작

    CHECK(setByPointer(*c, &col, "/shape", "sphere"));
    CHECK(col.shape == TestShape::Sphere);
    CHECK_FALSE(setByPointer(*c, &col, "/shape", "triangle"));      // enum 에 없음
    CHECK_FALSE(setByPointer(*c, &col, "/shape", 1));               // 정수 저장 금지 (§5.3)
    CHECK(setByPointer(*c, &col, "/offset/1", 2.5));
    CHECK(col.offset.y == 2.5f);
    CHECK(setByPointer(*c, &col, "/offset", Json::array({1, 2, 3})));
    CHECK(col.offset == Vec3{1, 2, 3});
    CHECK_FALSE(setByPointer(*c, &col, "/offset", Json::array({1, 2})));   // 길이 불일치
    CHECK(setByPointer(*c, &col, "/material", "asset_01j5xq8z3mf0n9k2c7p4rtvw6y#materials/rubber"));
    CHECK(col.material.subPart() == "materials/rubber");
    CHECK_FALSE(setByPointer(*c, &col, "/material", "not-an-id"));
    CHECK(setByPointer(*c, &col, "/priority", 3));
    CHECK_FALSE(setByPointer(*c, &col, "/priority", 3.5));          // int 에 float 금지
    CHECK(setByPointer(*c, &col, "/layer", "Enemy"));
    CHECK(col.layer == "Enemy");

    // ReadOnly 는 기본 거부, allowReadOnly 로 허용 (runtime 내부용)
    const ComponentMeta* h = Registry::global().find("TestHealth");
    TestHealth hp;
    CHECK_FALSE(setByPointer(*h, &hp, "/current", 5.0));
    CHECK(setByPointer(*h, &hp, "/current", 5.0, true));
    CHECK(hp.current == 5.0f);
}

TEST_CASE("Reflection: JSON Schema 2020-12 with x-* extensions (§14)") {
    const ComponentMeta* h = Registry::global().find("TestHealth");
    Json s = h->toSchema();
    CHECK(s["$schema"] == "https://json-schema.org/draft/2020-12/schema");
    CHECK(s["$id"] == "game://schema/component/TestHealth/1");
    CHECK(s["type"] == "object");
    CHECK(s["x-requires"] == Json::array({"Transform"}));
    CHECK(s["x-lifecycle"]["tick"] == "after EnemyAI");
    CHECK(s["additionalProperties"] == false);
    CHECK(s["required"] == Json::array({"max"}));
    const Json& pmax = s["properties"]["max"];
    CHECK(pmax["type"] == "number");
    CHECK(pmax["minimum"] == 1.0);
    CHECK(pmax["default"] == 100.0);
    CHECK(pmax["x-unit"] == "hp");
    CHECK(pmax["x-ui"]["widget"] == "slider");
    CHECK(pmax["x-cpp"] == "float");
    const Json& pcur = s["properties"]["current"];
    CHECK(pcur["readOnly"] == true);
    CHECK(pcur["x-runtimeOnly"] == true);
    CHECK(pcur["x-save"] == true);
    CHECK_FALSE(s["properties"]["invulnerable"].contains("x-runtimeOnly"));
    CHECK(s["properties"]["invulnerable"]["x-advanced"] == true);

    const ComponentMeta* c = Registry::global().find("TestCollider");
    Json sc = c->toSchema();
    CHECK(sc["properties"]["shape"]["enum"] == Json::array({"box", "sphere", "capsule"}));
    CHECK(sc["properties"]["shape"]["default"] == "capsule");
    CHECK(sc["properties"]["offset"]["type"] == "array");
    CHECK(sc["properties"]["offset"]["minItems"] == 3);
    CHECK(sc["properties"]["offset"]["maxItems"] == 3);
    CHECK(sc["properties"]["radius"]["x-warn"]["max"] == 10.0);
    CHECK(sc["properties"]["material"]["x-ref"] == "asset:material");
    CHECK(sc["properties"]["material"]["pattern"].get<std::string>().find("[0-7]") != std::string::npos);
    CHECK(sc["properties"]["priority"]["type"] == "integer");
    CHECK(sc["properties"]["priority"]["minimum"] == -10.0);
    // 키 순서: properties 가 선언 순서 (ordered_json)
    auto it = sc["properties"].begin();
    CHECK(it.key() == "shape"); ++it; CHECK(it.key() == "radius");
}

TEST_CASE("Reflection: wire_format lists spawn example and mutation paths (§14.1)") {
    const ComponentMeta* c = Registry::global().find("TestCollider");
    Json w = c->toWireFormat();
    CHECK(w["type"] == "TestCollider");
    CHECK(w["spawnExample"]["shape"] == "capsule");
    CHECK(w["spawnExample"]["offset"] == Json::array({0.0, 0.0, 0.0}));
    Json paths = w["mutationPaths"];
    auto hasPath = [&](const char* p) { for (const auto& x : paths) if (x == p) return true; return false; };
    CHECK(hasPath("/shape")); CHECK(hasPath("/offset")); CHECK(hasPath("/offset/0")); CHECK(hasPath("/offset/2"));
    CHECK_FALSE(hasPath("/offset/3"));
    CHECK(w["enumFormats"]["/shape"] == Json::array({"box", "sphere", "capsule"}));

    const ComponentMeta* h = Registry::global().find("TestHealth");
    Json wh = h->toWireFormat();
    CHECK(wh["readOnlyPaths"] == Json::array({"/current"}));
    CHECK_FALSE(wh["spawnExample"].contains("current"));   // authoring 예시에는 runtimeOnly 없음
}

TEST_CASE("Reflection: visibility masks (§88.8 유일한 정의)") {
    const ComponentMeta* h = Registry::global().find("TestHealth");
    Json authoring = h->defaultJson(Visibility::Authoring);
    Json snapshot = h->defaultJson(Visibility::Snapshot);
    Json save = h->defaultJson(Visibility::Save);
    CHECK(authoring.contains("max")); CHECK_FALSE(authoring.contains("current"));
    CHECK(snapshot.contains("max")); CHECK(snapshot.contains("current"));
    CHECK_FALSE(save.contains("max")); CHECK(save.contains("current"));
    CHECK(isVisible(PropFlags::Transient, Visibility::Snapshot) == false);
    CHECK(isVisible(PropFlags::Save | PropFlags::Transient, Visibility::Save) == false);
}

TEST_CASE("Reflection: duplicate registration is rejected") {
    ComponentBuilder<TestHealth> b("TestHealth", "dup");
    CHECK_FALSE(b.finish());
}
