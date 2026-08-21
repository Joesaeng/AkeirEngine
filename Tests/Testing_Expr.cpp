// Testing_Expr.cpp — 설계 문서 §23.1 고정 비교 문법: 파싱, 평가, undefined/has, 매크로, 오류
#include <doctest/doctest.h>
#include "pme/testing/Expr.h"

using namespace pme;
using pme::expr::Expr;

namespace {
std::map<std::string, Json> env() {
    std::map<std::string, Json> b;
    b["player"] = Json::parse(R"json({"Health":{"max":100,"current":42},"Transform":{"position":[1.5,-2,0]},"EnemyAI":null})json");
    b["goblin"] = Json::parse(R"json({"Health":{"max":30,"current":0},"EnemyAI":{"state":"Dead","target":"entity_x"},"Transform":{"position":[4,0,0]}})json");
    b["world"] = Json::parse(R"json({"tick":120,"entities":[
        {"id":"a","components":{"EnemyAI":{"state":"Chase"},"Collider2D":{"shape":"box"}}},
        {"id":"b","components":{"Transform":{"position":[0,0,0]}}},
        {"id":"c","components":{"EnemyAI":{"state":"Dead"},"Collider2D":{"shape":"circle"}}}]})json");
    return b;
}
bool ev(const char* text) {
    expr::ParseError pe;
    auto e = Expr::parse(text, &pe);
    REQUIRE_MESSAGE(e, pe.message);
    return e->evalBool(env());
}
Json val(const char* text) {
    expr::ParseError pe;
    auto e = Expr::parse(text, &pe);
    REQUIRE_MESSAGE(e, pe.message);
    return e->eval(env());
}
} // namespace

TEST_CASE("Expr: comparisons, arithmetic, logic, strings, lists") {
    CHECK(ev("player.Health.current > 0"));
    CHECK(ev("player.Health.current == 42"));
    CHECK(ev("player.Health.current == 42.0"));
    CHECK(ev("player.Health.current != player.Health.max"));
    CHECK(ev("player.Health.current * 2 + 16 == player.Health.max"));
    CHECK(ev("player.Health.max / 4 == 25"));
    CHECK(ev("goblin.EnemyAI.state == \"Dead\""));
    CHECK(ev("goblin.EnemyAI.state in [\"Dead\", \"Idle\"]"));
    CHECK(ev("\"Health\" in player"));
    CHECK(ev("!(goblin.Health.current > 0) && player.Health.current > 0 || false"));
    CHECK(ev("player.Transform.position[0] == 1.5 && player.Transform.position[1] < 0"));
    CHECK(ev("-player.Transform.position[1] == 2"));
    CHECK(ev("world.tick >= 120"));
    CHECK(val("size(world.entities)") == 3);
    CHECK(val("world.entities.size()") == 3);
    CHECK(val("abs(player.Transform.position[1])") == 2.0);
    CHECK(val("dist(player.Transform.position, goblin.Transform.position)") == doctest::Approx(3.2015621));
    CHECK(val("min(1, 2) + max(1, 2)") == 3);
    CHECK(val("7 % 4") == 3);
    CHECK(val("\"a\" + \"b\"") == "ab");
}

TEST_CASE("Expr: has() and undefined semantics (오타는 false 가 아니라 오류)") {
    CHECK(ev("has(player.Health)"));
    CHECK_FALSE(ev("has(player.Stamina)"));
    CHECK_FALSE(ev("has(player.EnemyAI)"));          // null 은 없는 것으로 (despawn 된 binding)
    CHECK_FALSE(ev("has(nobody.Health)"));
    CHECK(ev("!has(player.Stamina) || player.Stamina.value > 0"));   // 단락 평가
    CHECK_THROWS_AS(ev("player.Stamina.value > 0"), expr::EvalError);
    CHECK_THROWS_AS(ev("nobody.Health.current > 0"), expr::EvalError);
    CHECK_THROWS_AS(ev("player.Health.current > \"x\""), expr::EvalError);
    CHECK_THROWS_AS(ev("player.Health"), expr::EvalError);            // bool 아님
    CHECK_THROWS_AS(ev("1 / 0 == 1"), expr::EvalError);
    CHECK_THROWS_AS(ev("frobnicate(1)"), expr::EvalError);
}

TEST_CASE("Expr: quantifier macros over world.entities") {
    CHECK(ev("world.entities.all(e, !has(e.components.EnemyAI) || has(e.components.Collider2D))"));
    CHECK_THROWS_AS(ev("world.entities.exists(e, e.components.EnemyAI.state == \"Dead\")"), expr::EvalError);   // b 에는 EnemyAI 가 없다 → has() 로 막아야 한다
    CHECK(ev("world.entities.exists(e, has(e.components.EnemyAI) && e.components.EnemyAI.state == \"Dead\")"));
    CHECK(ev("world.entities.exists_one(e, has(e.components.EnemyAI) && e.components.EnemyAI.state == \"Chase\")"));
    CHECK_FALSE(ev("world.entities.all(e, has(e.components.EnemyAI))"));
    CHECK_FALSE(ev("world.entities.exists_one(e, has(e.components.Collider2D))"));   // 두 개
    CHECK_THROWS_AS(ev("world.tick.all(e, true)"), expr::EvalError);
}

TEST_CASE("Expr: parse errors carry an offset — probeBindings reports the values that were compared") {
    expr::ParseError pe;
    CHECK_FALSE(Expr::parse("player.Health.current >", &pe));
    CHECK(pe.message.find("unexpected end") != std::string::npos);
    CHECK_FALSE(Expr::parse("player.Health.current = 3", &pe));
    CHECK(Expr::parse("goblin.EnemyAI.state == Dead", &pe));                          // 따옴표 없는 Dead 는 식별자 → 파싱은 통과
    CHECK_THROWS_AS(ev("goblin.EnemyAI.state == Dead"), expr::EvalError);              // 평가 시점에 undefined 오류 (§23 초안의 실수를 잡는다)
    CHECK_FALSE(Expr::parse("(1 + 2", &pe));
    CHECK_FALSE(Expr::parse("world.entities.all(1, true)", &pe));

    auto e = Expr::parse("player.Health.current > goblin.Health.max && has(player.Stamina)");
    REQUIRE(e);
    Json probe = e->probeBindings(env());
    CHECK(probe["player.Health.current"] == 42);
    CHECK(probe["goblin.Health.max"] == 30);
    CHECK(probe["player.Stamina"] == "<undefined>");
    auto roots = e->roots();
    CHECK(roots == std::vector<std::string>{"goblin", "player"});
}
