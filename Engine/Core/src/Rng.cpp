// pme/core/Rng.cpp — xoshiro256** (Blackman & Vigna, public domain 알고리즘) + SplitMix64 seeding. 설계 문서 §22.2.
#include "pme/core/Rng.h"
#include "pme/core/Hash.h"

namespace pme {

namespace {
inline std::uint64_t rotl(std::uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
}

RngStream::RngStream(std::uint64_t worldSeed, std::string_view streamName) {
    reseed(hash64Combine(worldSeed, fnv1a64(streamName)));
}

void RngStream::reseed(std::uint64_t seed) {
    // "initialization must be performed with a generator radically different in nature" — SplitMix64 (prng.di.unimi.it)
    std::uint64_t x = seed;
    for (auto& s : s_) { x += 0x9E3779B97F4A7C15ULL; s = splitMix64(x - 0x9E3779B97F4A7C15ULL) ^ x; }
    if (s_[0] == 0 && s_[1] == 0 && s_[2] == 0 && s_[3] == 0) s_[0] = 1; // all-zero state 금지
}

std::uint64_t RngStream::next() {
    const std::uint64_t result = rotl(s_[1] * 5, 7) * 9;
    const std::uint64_t t = s_[1] << 17;
    s_[2] ^= s_[0]; s_[3] ^= s_[1]; s_[1] ^= s_[2]; s_[0] ^= s_[3];
    s_[2] ^= t;
    s_[3] = rotl(s_[3], 45);
    return result;
}

double RngStream::nextDouble() { return static_cast<double>(next() >> 11) * 0x1.0p-53; }
float RngStream::nextFloat() { return static_cast<float>(next() >> 40) * 0x1.0p-24f; }

std::uint64_t RngStream::nextRange(std::uint64_t n) {
    if (n <= 1) return 0;
    // rejection sampling — 편향 없음, 결정적
    const std::uint64_t limit = ~std::uint64_t(0) - (~std::uint64_t(0) % n);
    std::uint64_t r;
    do { r = next(); } while (r >= limit);
    return r % n;
}

std::int64_t RngStream::nextInt(std::int64_t lo, std::int64_t hi) {
    if (hi <= lo) return lo;
    const std::uint64_t span = static_cast<std::uint64_t>(hi - lo) + 1;
    return lo + static_cast<std::int64_t>(nextRange(span));
}

float RngStream::nextFloat(float lo, float hi) { return lo + (hi - lo) * nextFloat(); }
bool RngStream::nextBool(float p) { return nextFloat() < p; }

void RngStream::jump() {
    static const std::uint64_t JUMP[] = {0x180ec6d33cfd0abaULL, 0xd5a61266f0c9392cULL, 0xa9582618e03fc9aaULL, 0x39abdc4529b1661cULL};
    std::uint64_t s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    for (std::uint64_t j : JUMP)
        for (int b = 0; b < 64; ++b) {
            if (j & (std::uint64_t(1) << b)) { s0 ^= s_[0]; s1 ^= s_[1]; s2 ^= s_[2]; s3 ^= s_[3]; }
            next();
        }
    s_ = {s0, s1, s2, s3};
}

} // namespace pme
