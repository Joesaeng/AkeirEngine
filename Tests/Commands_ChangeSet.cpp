// Commands_ChangeSet.cpp — 설계 문서 §78 (ChangeSet RFC 6902 superset, self-inverting), §10.1 (inverse), §9.2 (compose)
#include "akeir/commands/ChangeSet.h"
#include "akeir/serialization/Canonical.h"

#include <doctest/doctest.h>

using namespace akeir;

namespace {
std::map<std::string, Json> sampleDocs() {
    std::map<std::string, Json> d;
    d["Worlds/A.world.json"] = Json::parse(R"({"id":"world_x","entities":{"entity_1":{"name":"A","components":{"Health":{"max":10}}}}})");
    return d;
}
} // namespace

TEST_CASE("ChangeSet: ops apply and inverse restores the document byte-for-byte (§78, §10.1)") {
    auto docs = sampleDocs();
    const Json original = docs["Worlds/A.world.json"];
    ChangeSet cs;
    cs.id = "cs_test";
    cs.actor = "test";
    const std::string doc = "Worlds/A.world.json";
    cs.ops.push_back({"replace", doc, "/entities/entity_1/components/Health/max", "", 20, 10});
    cs.ops.push_back({"add", doc, "/entities/entity_1/components/Movement", "", Json{{"speed", 3}}, {}});
    cs.ops.push_back({"remove", doc, "/entities/entity_1/name", "", {}, "A"});
    cs.ops.push_back({"move", doc, "/entities/entity_1/tags", "/entities/entity_1/components/Movement", {}, Json{{"speed", 3}}});
    cs.finalize();
    CHECK(cs.touched == std::vector<std::string>{doc});

    std::vector<Diagnostic> diags;
    REQUIRE(applyOps(docs, cs.ops, &diags));
    CHECK(docs[doc]["entities"]["entity_1"]["components"]["Health"]["max"] == 20);
    CHECK(!docs[doc]["entities"]["entity_1"].contains("name"));
    CHECK(docs[doc]["entities"]["entity_1"]["tags"]["speed"] == 3);

    ChangeSet inv = cs.inverse();
    CHECK(inv.ops.size() == 4);
    CHECK(inv.ops[0].op == "move");
    CHECK(inv.ops[3].op == "replace");
    CHECK(inv.ops[3].value == 10);
    CHECK(inv.ops[3].before == 20);
    REQUIRE(applyOps(docs, inv.ops, &diags));
    // ordered_json 은 키 순서까지 비교한다. remove→add 로 되살린 키는 뒤에 붙으므로 값 동등성은 JCS(키 정렬)로 본다.
    // 파일로 갈 때는 Project::canonicalizeDocument 가 §5.3 순서로 다시 정렬하므로 byte-identical 이 된다 (Commands_Bus 테스트가 확인).
    CHECK(jcsDump(docs[doc]) == jcsDump(original));

    // inverse(inverse) == 원본 ops 와 같은 효과
    ChangeSet again = inv.inverse();
    REQUIRE(applyOps(docs, again.ops, &diags));
    CHECK(docs[doc]["entities"]["entity_1"]["components"]["Health"]["max"] == 20);
}

TEST_CASE("ChangeSet: before mismatch is rejected (§10.2 conflict)") {
    auto docs = sampleDocs();
    ChangeOp o{"replace", "Worlds/A.world.json", "/entities/entity_1/components/Health/max", "", 5, 999};
    std::vector<Diagnostic> diags;
    CHECK_FALSE(applyOps(docs, {o}, &diags));
    REQUIRE(diags.size() == 1);
    CHECK(diags[0].ruleId == "CHANGESET_BEFORE_MISMATCH");
    CHECK(docs["Worlds/A.world.json"]["entities"]["entity_1"]["components"]["Health"]["max"] == 10);
}

TEST_CASE("ChangeSet: JSON round trip and RFC 6902 projection (§78 규칙 5)") {
    ChangeSet cs;
    cs.id = "cs_1"; cs.actor = "ai:claude#1"; cs.createdAt = "2026-08-21T00:00:00Z";
    cs.intent = Json{{"op", "property.set"}};
    cs.ops.push_back({"replace", "Prefabs/G.prefab.json", "/components/Health/max", "", 80, 30});
    cs.finalize();
    Json j = cs.toJson();
    CHECK(j["ops"][0]["value"] == 80);
    CHECK(j["ops"][0]["before"] == 30);
    CHECK(j["summary"]["replace"] == 1);
    auto back = ChangeSet::fromJson(j);
    REQUIRE(back);
    CHECK(back->toJson() == j);

    Json rfc = cs.ops[0].toRfc6902();
    CHECK(rfc == Json{{"op", "replace"}, {"path", "/components/Health/max"}, {"value", 80}});
    Json doc = Json{{"components", Json{{"Health", Json{{"max", 30}}}}}};
    CHECK(doc.patch(Json::array({rfc}))["components"]["Health"]["max"] == 80);
}

TEST_CASE("ChangeSet: compose keeps order and first base (§9.2)") {
    ChangeSet a; a.id = "cs_a"; a.actor = "x"; a.base = Json{{"d", "sha256:1"}}; a.ops.push_back({"add", "d", "/k", "", 1, {}}); a.finalize();
    ChangeSet b; b.id = "cs_b"; b.actor = "x"; b.base = Json{{"d", "sha256:2"}}; b.ops.push_back({"replace", "d", "/k", "", 2, 1}); b.finalize();
    ChangeSet c = ChangeSet::compose({a, b}, "tx_1");
    CHECK(c.tx == "tx_1");
    CHECK(c.ops.size() == 2);
    CHECK(c.base["d"] == "sha256:1");
    CHECK(c.intent.is_array());
    CHECK(c.intent.size() == 2);
}

TEST_CASE("ChangeSet: document-level add/remove and pointer escaping") {
    std::map<std::string, Json> docs;
    std::vector<Diagnostic> diags;
    REQUIRE(applyOps(docs, {ChangeOp{"add", "Worlds/New.world.json", "", "", Json{{"id", "world_n"}}, {}}}, &diags));
    CHECK(docs.count("Worlds/New.world.json") == 1);
    REQUIRE(applyOps(docs, {ChangeOp{"add", "Worlds/New.world.json", "/set", "", Json::object(), {}}}, &diags));
    REQUIRE(applyOps(docs, {ChangeOp{"add", "Worlds/New.world.json", "/set/" + escapeToken("/components/Health/max"), "", 5, {}}}, &diags));
    CHECK(docs["Worlds/New.world.json"]["set"]["/components/Health/max"] == 5);
    CHECK(escapeToken("a/b~c") == "a~1b~0c");
    CHECK(unescapeToken("a~1b~0c") == "a/b~c");
    REQUIRE(applyOps(docs, {ChangeOp{"remove", "Worlds/New.world.json", "", "", {}, docs["Worlds/New.world.json"]}}, &diags));
    CHECK(docs.empty());
}
