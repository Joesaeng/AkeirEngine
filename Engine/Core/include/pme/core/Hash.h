// pme/core/Hash.h — 결정적 해시. 설계 문서 §22.2 (world hash: FNV-1a/xxHash64), §37/§52 (cache key / checkpoint: SHA-256).
//
// sim 코드에서 std::hash 는 금지다(플랫폼 의존, §22.2). 여기 함수들은 입력 바이트에만 의존한다.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace pme {

/// FNV-1a 64-bit. 짧은 키/상태 해시용. 암호학적 용도 아님.
constexpr std::uint64_t kFnvOffset64 = 0xcbf29ce484222325ULL;
constexpr std::uint64_t kFnvPrime64 = 0x100000001b3ULL;

constexpr std::uint64_t fnv1a64(std::string_view s, std::uint64_t h = kFnvOffset64) {
    for (unsigned char c : s) { h ^= c; h *= kFnvPrime64; }
    return h;
}
std::uint64_t fnv1a64(const void* data, std::size_t size, std::uint64_t h = kFnvOffset64);

/// SplitMix64 — seed 확산용 (xoshiro 초기화, 결정적 ID 파생). §22.2 RNG.
constexpr std::uint64_t splitMix64(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

/// 두 64-bit 해시를 섞는다 (순서 의존).
constexpr std::uint64_t hash64Combine(std::uint64_t a, std::uint64_t b) {
    return splitMix64(a ^ (b + 0x9E3779B97F4A7C15ULL + (a << 6) + (a >> 2)));
}

/// 증분 world-hash 빌더 (§22.2 Verification). float 은 bit pattern 으로 넣는다.
class Hasher {
public:
    void bytes(const void* data, std::size_t size) { h_ = fnv1a64(data, size, h_); }
    void u64(std::uint64_t v) { bytes(&v, sizeof v); }
    void i64(std::int64_t v) { bytes(&v, sizeof v); }
    void u32(std::uint32_t v) { bytes(&v, sizeof v); }
    void f32(float v) { bytes(&v, sizeof v); }   // bit pattern (§22.2, §26.1)
    void f64(double v) { bytes(&v, sizeof v); }
    void str(std::string_view s) { u64(s.size()); bytes(s.data(), s.size()); }
    std::uint64_t value() const { return h_; }
private:
    std::uint64_t h_ = kFnvOffset64;
};

/// SHA-256 (FIPS 180-4). 파일 내용 해시, cache key(§37), checkpoint digest(§52), ChangeSet base(§9.2).
class Sha256 {
public:
    Sha256();
    void update(const void* data, std::size_t size);
    void update(std::string_view s) { update(s.data(), s.size()); }
    std::array<std::uint8_t, 32> finish();
    /// "sha256:<64 hex>" 형태 (문서 전반에서 쓰는 표기)
    static std::string hexOfBytes(const void* data, std::size_t size, bool withPrefix = true);
    static std::string hexOf(std::string_view s, bool withPrefix = true) { return hexOfBytes(s.data(), s.size(), withPrefix); }
private:
    void transform(const std::uint8_t block[64]);
    std::uint32_t state_[8];
    std::uint64_t bitLen_ = 0;
    std::uint8_t buffer_[64];
    std::size_t bufferLen_ = 0;
};

std::string toHex(const std::uint8_t* data, std::size_t size);
std::string toHex64(std::uint64_t v); // "0x%016llx"

} // namespace pme
