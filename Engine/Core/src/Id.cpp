// akeir/core/Id.cpp — 설계 문서 §7.1
#include "akeir/core/Id.h"
#include "akeir/core/Hash.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <mutex>
#include <random>

namespace akeir {

namespace {

// 128-bit 값을 big-endian 비트 배열처럼 다루는 헬퍼. pos 는 LSB 기준 비트 위치.
std::uint32_t getBits(const Uuid& u, int pos, int count) {
    std::uint32_t v = 0;
    for (int i = count - 1; i >= 0; --i) {
        int bit = pos + i;
        if (bit >= 128) { v <<= 1; continue; }
        int byteIdx = 15 - (bit / 8);
        int bitIdx = bit % 8;
        v = (v << 1) | ((u.bytes[static_cast<std::size_t>(byteIdx)] >> bitIdx) & 1u);
    }
    return v;
}

void setBits(Uuid& u, int pos, int count, std::uint32_t value) {
    for (int i = 0; i < count; ++i) {
        int bit = pos + i;
        if (bit >= 128) continue;
        int byteIdx = 15 - (bit / 8);
        int bitIdx = bit % 8;
        std::uint8_t mask = static_cast<std::uint8_t>(1u << bitIdx);
        if ((value >> i) & 1u) u.bytes[static_cast<std::size_t>(byteIdx)] |= mask;
        else u.bytes[static_cast<std::size_t>(byteIdx)] &= static_cast<std::uint8_t>(~mask);
    }
}

std::array<int, 256> buildDecodeTable() {
    std::array<int, 256> t{};
    t.fill(-1);
    for (std::size_t i = 0; i < Id::kAlphabet.size(); ++i)
        t[static_cast<unsigned char>(Id::kAlphabet[i])] = static_cast<int>(i);
    return t;
}

const std::array<int, 256>& decodeTable() {
    static const std::array<int, 256> t = buildDecodeTable();
    return t;
}

std::mt19937_64& authoringRng() {
    // 비결정적 난수: authoring ID 발급 전용. sim 코드에서는 절대 쓰지 않는다 (§22.2 RNG 규칙).
    static std::mt19937_64 rng = [] {
        std::random_device rd;
        std::seed_seq seq{rd(), rd(), rd(), rd(), rd(), rd(), rd(), rd()};
        return std::mt19937_64(seq);
    }();
    return rng;
}

} // namespace

// ---------------------------------------------------------------- base32

std::string encodeBase32(const Uuid& uuid) {
    std::string out(Id::kSuffixLength, '0');
    for (std::size_t i = 0; i < Id::kSuffixLength; ++i) {
        int pos = static_cast<int>((Id::kSuffixLength - 1 - i) * 5);
        out[i] = Id::kAlphabet[getBits(uuid, pos, 5)];
    }
    return out;
}

std::optional<Uuid> decodeBase32(std::string_view suffix) {
    if (suffix.size() != Id::kSuffixLength) return std::nullopt;
    const auto& table = decodeTable();
    Uuid u{};
    for (std::size_t i = 0; i < Id::kSuffixLength; ++i) {
        int v = table[static_cast<unsigned char>(suffix[i])];
        if (v < 0) return std::nullopt;
        if (i == 0 && v > 7) return std::nullopt; // 첫 글자 0–7: 130 bits → 128 bits 오버플로 방지
        int pos = static_cast<int>((Id::kSuffixLength - 1 - i) * 5);
        setBits(u, pos, 5, static_cast<std::uint32_t>(v));
    }
    return u;
}

// ---------------------------------------------------------------- UUID

Uuid makeUuidV7() {
    static std::mutex mtx;
    static std::uint64_t lastMs = 0;
    static std::uint32_t counter = 0; // 12-bit, RFC 9562 §6.2 method 1

    std::lock_guard<std::mutex> lock(mtx);
    using namespace std::chrono;
    std::uint64_t ms = static_cast<std::uint64_t>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
    if (ms <= lastMs) {
        ms = lastMs;
        if (++counter > 0x0FFF) { counter = 0; ms = ++lastMs; } // counter 소진 시 ms 를 앞당긴다 (단조성 유지)
    } else {
        lastMs = ms;
        counter = static_cast<std::uint32_t>(authoringRng()() & 0x07FF); // 상위 비트 0 → 여유 확보
    }

    Uuid u{};
    for (int i = 0; i < 6; ++i) u.bytes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(ms >> (8 * (5 - i)));
    u.bytes[6] = static_cast<std::uint8_t>(0x70 | ((counter >> 8) & 0x0F));
    u.bytes[7] = static_cast<std::uint8_t>(counter & 0xFF);
    std::uint64_t r = authoringRng()();
    for (int i = 0; i < 8; ++i) u.bytes[static_cast<std::size_t>(8 + i)] = static_cast<std::uint8_t>(r >> (8 * (7 - i)));
    u.bytes[8] = static_cast<std::uint8_t>(0x80 | (u.bytes[8] & 0x3F)); // variant 10
    return u;
}

Uuid makeUuidV8(std::uint64_t hi, std::uint64_t lo) {
    Uuid u{};
    for (int i = 0; i < 8; ++i) {
        u.bytes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(hi >> (8 * (7 - i)));
        u.bytes[static_cast<std::size_t>(8 + i)] = static_cast<std::uint8_t>(lo >> (8 * (7 - i)));
    }
    u.bytes[6] = static_cast<std::uint8_t>(0x80 | (u.bytes[6] & 0x0F)); // version 8
    u.bytes[8] = static_cast<std::uint8_t>(0x80 | (u.bytes[8] & 0x3F)); // variant 10
    return u;
}

// ---------------------------------------------------------------- Id

Id::Id(std::string_view prefix, const Uuid& uuid) : uuid_(uuid) {
    text_.reserve(prefix.size() + 1 + kSuffixLength);
    text_.append(prefix);
    text_.push_back('_');
    text_.append(encodeBase32(uuid));
    prefixLen_ = prefix.size();
}

Id Id::generate(std::string_view prefix) {
    return Id(prefix, makeUuidV7());
}

Id Id::deterministic(std::string_view prefix, std::uint64_t worldSeed, std::uint64_t tick, std::uint64_t ordinal) {
    // 결정적 파생: 입력 4개를 순서대로 섞는다. 같은 입력 → 같은 UUID (§7.1, §22.2).
    std::uint64_t h = hash64Combine(worldSeed, fnv1a64(prefix));
    h = hash64Combine(h, tick);
    h = hash64Combine(h, ordinal);
    std::uint64_t hi = splitMix64(h);
    std::uint64_t lo = splitMix64(hi ^ 0x9E3779B97F4A7C15ULL);
    return Id(prefix, makeUuidV8(hi, lo));
}

bool Id::isValidPrefix(std::string_view p) {
    if (p.empty() || p.size() > 63) return false;
    auto lower = [](char c) { return c >= 'a' && c <= 'z'; };
    if (!lower(p.front()) || !lower(p.back())) return false;
    return std::all_of(p.begin(), p.end(), [&](char c) { return lower(c) || c == '_'; });
}

std::string Id::validate(std::string_view text) {
    if (text.size() < kSuffixLength + 2) return "too short: expected <prefix>_<26 chars>";
    std::size_t sep = text.size() - kSuffixLength - 1;
    if (text[sep] != '_') return "missing '_' separator before the 26-char suffix";
    std::string prefix(text.substr(0, sep));
    std::transform(prefix.begin(), prefix.end(), prefix.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!isValidPrefix(prefix)) return "invalid prefix: must match ^[a-z]([a-z_]{0,61}[a-z])?$";
    std::string suffix(text.substr(sep + 1));
    std::transform(suffix.begin(), suffix.end(), suffix.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto uuid = decodeBase32(suffix);
    if (!uuid) return "invalid suffix: 26 chars of Crockford base32 (0-9a-hjkmnp-tv-z), first char 0-7";
    if (!uuid->isRfc()) return "suffix does not decode to an RFC 9562 UUID (variant bits)";
    if (uuid->version() != 7 && uuid->version() != 8) return "suffix UUID version must be 7 (authoring) or 8 (runtime-spawned)";
    return {};
}

std::optional<Id> Id::parse(std::string_view text) {
    if (!validate(text).empty()) return std::nullopt;
    std::size_t sep = text.size() - kSuffixLength - 1;
    std::string prefix(text.substr(0, sep));
    std::string suffix(text.substr(sep + 1));
    auto low = [](std::string& s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); };
    low(prefix); low(suffix);
    return Id(prefix, *decodeBase32(suffix));
}

bool Id::matchesShortForm(std::string_view shortForm) const {
    // "<prefix>_<some leading chars of suffix>" — git 식 고유 prefix 선택 (§7.4)
    std::string s(shortForm);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text_.size() >= s.size() && text_.compare(0, s.size(), s) == 0 && s.size() > prefixLen_ + 1;
}

} // namespace akeir
