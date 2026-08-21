// Serialization_Canonical.cpp — 설계 문서 §5.3 (canonical JSON, 숫자 규칙, JCS), §9.2 (temp+rename), §29 (JSON_NOT_CANONICAL)
#include <doctest/doctest.h>
#include "pme/serialization/Canonical.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace pme;

TEST_CASE("Canonical: objects multi-line, scalar arrays inline, 2-space indent, trailing newline (§5.3)") {
    Json doc = Json::object();
    doc["$schema"] = "game://schema/prefab/1";
    doc["id"] = "prefab_01j5xq8z3mf0n9k2c7p4rtvw6y";
    doc["components"] = Json::object();
    doc["components"]["Transform"] = Json{{"position", Json::array({0, 0, 0})}, {"rotation", Json::array({0, 0, 0, 1})}};
    doc["tags"] = Json::array({"enemy"});
    doc["list"] = Json::array({Json{{"a", 1}}, Json{{"b", 2}}});
    auto text = canonicalDump(doc);
    REQUIRE(text.has_value());
    const std::string expected =
        "{\n"
        "  \"$schema\": \"game://schema/prefab/1\",\n"
        "  \"id\": \"prefab_01j5xq8z3mf0n9k2c7p4rtvw6y\",\n"
        "  \"components\": {\n"
        "    \"Transform\": {\n"
        "      \"position\": [0, 0, 0],\n"
        "      \"rotation\": [0, 0, 0, 1]\n"
        "    }\n"
        "  },\n"
        "  \"tags\": [\"enemy\"],\n"
        "  \"list\": [\n"
        "    {\n"
        "      \"a\": 1\n"
        "    },\n"
        "    {\n"
        "      \"b\": 2\n"
        "    }\n"
        "  ]\n"
        "}\n";
    CHECK(*text == expected);
    CHECK(isCanonicalText(*text, doc));
    CHECK_FALSE(isCanonicalText(doc.dump(2), doc));   // nlohmann pretty 는 canonical 이 아니다 (배열 줄바꿈)
}

TEST_CASE("Canonical: float32 shortest round-trip, integers without .0, NaN rejected (§5.3)") {
    CHECK(canonicalizeFloat(0.3f) == 0.3);
    CHECK(canonicalizeFloat(0.1f) == 0.1);
    CHECK(static_cast<float>(canonicalizeFloat(0.1f)) == 0.1f);   // round-trip 보존
    Json j = Json::object();
    j["speed"] = canonicalizeFloat(4.8f);
    j["whole"] = 2.0;
    j["neg"] = canonicalizeFloat(-0.25f);
    j["big"] = 123456789;
    auto t = canonicalDump(j);
    REQUIRE(t);
    CHECK(*t == "{\n  \"speed\": 4.8,\n  \"whole\": 2,\n  \"neg\": -0.25,\n  \"big\": 123456789\n}\n");
    // canonicalizeFloats 는 트리를 재귀 정규화
    Json raw = Json{{"v", Json::array({static_cast<double>(0.3f), 1.0, 2.5})}};
    Json c = canonicalizeFloats(raw);
    CHECK(c["v"][0] == 0.3);
    Json bad = Json{{"x", std::nan("")}};
    CHECK_FALSE(canonicalDump(bad).has_value());
}

TEST_CASE("Canonical: JCS sorts keys by UTF-16 code units and strips whitespace (RFC 8785)") {
    Json doc = Json::object();
    doc["b"] = 1;
    doc["a"] = Json{{"z", true}, {"y", Json::array({1.0, 2.5})}};
    doc["é"] = "x";   // U+00E9 > 'b'
    std::string jcs = jcsDump(doc);
    CHECK(jcs == "{\"a\":{\"y\":[1,2.5],\"z\":true},\"b\":1,\"é\":\"x\"}");
    // 같은 내용, 다른 삽입 순서 → 같은 JCS (해시 안정)
    Json doc2 = Json::object();
    doc2["é"] = "x"; doc2["a"] = Json{{"y", Json::array({1.0, 2.5})}, {"z", true}}; doc2["b"] = 1;
    CHECK(jcsDump(doc2) == jcs);
}

TEST_CASE("Canonical: parse rejects comments and trailing commas; file write is temp+rename and round-trips") {
    std::string err;
    CHECK_FALSE(parseJson("{ \"a\": 1, // no comments\n \"b\": 2 }", &err).has_value());
    CHECK(err.find("byte") != std::string::npos);
    CHECK_FALSE(parseJson("{ \"a\": 1, }", &err).has_value());
    auto ok = parseJson("{ \"a\": 1 }", &err);
    REQUIRE(ok);
    CHECK((*ok)["a"] == 1);

    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "pme_canonical_test";
    fs::create_directories(dir);
    fs::path file = dir / "sub" / "doc.json";
    Json doc = Json{{"id", "world_01j5xq8z3mf0n9k2c7p4rtvw6y"}, {"entities", Json::object()}};
    REQUIRE(writeCanonicalFile(file.string(), doc, &err));
    CHECK_FALSE(fs::exists(file.string() + ".tmp"));
    std::stringstream ss;
    { std::ifstream in(file, std::ios::binary); ss << in.rdbuf(); }   // remove_all 전에 닫는다
    CHECK(ss.str() == *canonicalDump(doc));
    CHECK(ss.str().back() == '\n');
    CHECK(ss.str().find('\r') == std::string::npos);   // LF only
    auto back = readJsonFile(file.string(), &err);
    REQUIRE(back);
    CHECK(*back == doc);
    fs::remove_all(dir);
}
