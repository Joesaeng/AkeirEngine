// Core_Id.cpp — 설계 문서 §7.1 TypeID / UUIDv7 / UUIDv8
#include <doctest/doctest.h>
#include "pme/core/Id.h"

#include <cctype>
#include <set>
#include <vector>

using namespace pme;

TEST_CASE("Id: generate produces TypeID grammar with UUIDv7 (§7.1)") {
    Id id = Id::generate("entity");
    CHECK(id.str().size() == 6 + 1 + 26);
    CHECK(id.prefix() == "entity");
    CHECK(id.suffix().size() == 26);
    CHECK(id.uuid().version() == 7);
    CHECK(id.uuid().isRfc());
    CHECK(Id::validate(id.str()).empty());
    CHECK(id.suffix()[0] >= '0');
    CHECK(id.suffix()[0] <= '7');
    for (char c : id.suffix()) CHECK(Id::kAlphabet.find(c) != std::string_view::npos);
}

TEST_CASE("Id: base32 round-trip is lossless") {
    Uuid u = makeUuidV7();
    auto enc = encodeBase32(u);
    auto dec = decodeBase32(enc);
    REQUIRE(dec.has_value());
    CHECK(*dec == u);
    Uuid all; all.bytes.fill(0xFF);
    auto e2 = encodeBase32(all);
    CHECK(e2[0] == '7');
    CHECK(decodeBase32(e2) == all);
    Uuid zero{};
    CHECK(encodeBase32(zero) == std::string(26, '0'));
}

TEST_CASE("Id: parse normalizes case and rejects bad input") {
    Id id = Id::generate("prefab");
    std::string upper = id.str();
    for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    auto parsed = Id::parse(upper);
    REQUIRE(parsed.has_value());
    CHECK(*parsed == id);

    CHECK_FALSE(Id::validate("entity").empty());
    CHECK_FALSE(Id::validate("entity_01j5xq8z3mf0n9k2c7p4rtvw6").empty());   // 25 chars
    CHECK_FALSE(Id::validate("entity_81j5xq8z3mf0n9k2c7p4rtvw6y").empty());  // first char 8 -> overflow
    CHECK_FALSE(Id::validate("entity_01j5xq8z3mf0n9k2c7p4rtvwiy").empty());  // 'i' not in alphabet
    CHECK_FALSE(Id::validate("_01j5xq8z3mf0n9k2c7p4rtvw6y").empty());        // empty prefix
    CHECK_FALSE(Id::validate("entity__01j5xq8z3mf0n9k2c7p4rtvw6y").empty()); // prefix ends with '_'
}

TEST_CASE("Id: v7 ids are monotonic and unique within a process") {
    std::vector<std::string> ids;
    for (int i = 0; i < 2000; ++i) ids.push_back(Id::generate("entity").str());
    for (std::size_t i = 1; i < ids.size(); ++i) CHECK(ids[i - 1] < ids[i]);
    CHECK(std::set<std::string>(ids.begin(), ids.end()).size() == ids.size());
}

TEST_CASE("Id: deterministic (v8) ids depend only on inputs (§7.1, §22.2)") {
    Id a = Id::deterministic("entity", 381251, 813, 0);
    Id b = Id::deterministic("entity", 381251, 813, 0);
    Id c = Id::deterministic("entity", 381251, 813, 1);
    Id d = Id::deterministic("entity", 381252, 813, 0);
    CHECK(a == b);
    CHECK(a != c);
    CHECK(a != d);
    CHECK(a.uuid().version() == 8);
    CHECK(Id::validate(a.str()).empty());
    // validator accepts only v7/v8 — a v4 suffix is rejected
    Uuid v4 = a.uuid();
    v4.bytes[6] = static_cast<std::uint8_t>(0x40 | (v4.bytes[6] & 0x0F));
    CHECK_FALSE(Id::validate(Id("entity", v4).str()).empty());
}

TEST_CASE("Id: short form prefix match (§7.4)") {
    Id id = Id::generate("entity");
    CHECK(id.matchesShortForm(id.str().substr(0, 12)));
    CHECK_FALSE(id.matchesShortForm("entity_"));
    CHECK_FALSE(id.matchesShortForm("prefab_01"));
}
