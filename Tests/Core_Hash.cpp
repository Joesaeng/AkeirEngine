// Core_Hash.cpp — 설계 문서 §22.2 (FNV-1a), §37/§52 (SHA-256)
#include <doctest/doctest.h>
#include "akeir/core/Hash.h"

using namespace akeir;

TEST_CASE("Hash: SHA-256 known answers (FIPS 180-4)") {
    CHECK(Sha256::hexOf("", false) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(Sha256::hexOf("abc", false) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(Sha256::hexOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", false) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(Sha256::hexOf("abc").rfind("sha256:", 0) == 0);
    Sha256 s;
    std::string chunk(1000, 'a');
    for (int i = 0; i < 1000; ++i) s.update(chunk);
    auto d = s.finish();
    CHECK(toHex(d.data(), d.size()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("Hash: FNV-1a 64 known answers") {
    CHECK(fnv1a64("") == 0xcbf29ce484222325ULL);
    CHECK(fnv1a64("a") == 0xaf63dc4c8601ec8cULL);
    CHECK(fnv1a64("foobar") == 0x85944171f73967e8ULL);
}

TEST_CASE("Hash: Hasher uses float bit patterns (§22.2)") {
    Hasher a, b;
    a.f32(0.1f);
    b.f32(0.1f);
    CHECK(a.value() == b.value());
    Hasher c;
    c.f32(-0.0f);
    Hasher d;
    d.f32(0.0f);
    CHECK(c.value() != d.value()); // different bit pattern -> different hash (intentional strictness)
    CHECK(toHex64(0x1234).rfind("0x", 0) == 0);
    CHECK(toHex64(0x1234) == "0x0000000000001234");
}
