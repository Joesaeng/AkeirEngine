// pme/core/Id.h — Persistent ID. 설계 문서 §7.1 (TypeID v0.3 grammar / RFC 9562 UUIDv7·v8).
//
//   Persistent ID = <type>_<26-char Crockford base32 of a 128-bit UUID>
//     prefix : ^[a-z]([a-z_]{0,61}[a-z])?$     구분자 "_" 하나
//     suffix : 정확히 26자, alphabet 0123456789abcdefghjkmnpqrstvwxyz, 첫 글자 0–7
//     suffix 는 유효한 UUID 로 디코드되어야 한다. authoring ID = UUIDv7, runtime-spawned ID = UUIDv8 (결정적).
//     출력은 소문자 고정. 입력은 소문자로 정규화한 뒤 검사한다 (규격 확장).
//
//   UUIDv7 생성: 48-bit Unix ms + 12-bit monotonic counter (RFC 9562 §6.2 method 1) + 62-bit random.
//   UUIDv8 생성: (worldSeed, tick, ordinal) 에서 결정적으로 파생 — §7.1 "결정적 런타임에서의 ID 생성".
//
// Runtime id(ECS entity) 는 이 타입과 무관하며 외부로 노출하지 않는다 (§7).
#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace pme {

/// 128-bit UUID (RFC 9562). 바이트는 big-endian 순서(네트워크 순서)로 보관한다.
struct Uuid {
    std::array<std::uint8_t, 16> bytes{};

    int version() const { return (bytes[6] >> 4) & 0x0F; }
    int variant() const { return (bytes[8] >> 6) & 0x03; } // 0b10 = RFC 9562
    bool isRfc() const { return variant() == 0b10; }

    auto operator<=>(const Uuid&) const = default;
};

/// TypeID 형태의 persistent id. 값 타입이며 문자열 표현이 정규형이다.
class Id {
public:
    static constexpr std::size_t kSuffixLength = 26;
    static constexpr std::string_view kAlphabet = "0123456789abcdefghjkmnpqrstvwxyz";

    Id() = default;
    Id(std::string_view prefix, const Uuid& uuid);

    /// 새 authoring ID (UUIDv7 + 프로세스 monotonic counter). 비결정적 — authoring 경로에서만 쓴다.
    static Id generate(std::string_view prefix);

    /// 결정적 runtime-spawned ID (UUIDv8). 같은 (worldSeed, tick, ordinal, prefix) → 같은 ID (§7.1, §22.2).
    static Id deterministic(std::string_view prefix, std::uint64_t worldSeed, std::uint64_t tick, std::uint64_t ordinal);

    /// 문자열 파싱. 대문자는 소문자로 정규화한다. 실패 시 nullopt (ID_FORMAT_INVALID, §29).
    static std::optional<Id> parse(std::string_view text);

    /// 파싱 실패 이유를 사람이 읽을 수 있게 돌려준다 (Diagnostic message 용). 성공이면 빈 문자열.
    static std::string validate(std::string_view text);

    static bool isValidPrefix(std::string_view prefix);

    const std::string& str() const { return text_; }
    std::string_view prefix() const { return std::string_view(text_).substr(0, prefixLen_); }
    std::string_view suffix() const { return std::string_view(text_).substr(prefixLen_ + 1); }
    const Uuid& uuid() const { return uuid_; }
    bool empty() const { return text_.empty(); }

    /// 같은 prefix 이고 suffix 가 주어진 prefix 로 시작하면 true (git 식 고유 prefix 선택, §7.4).
    bool matchesShortForm(std::string_view shortForm) const;

    auto operator<=>(const Id& o) const { return text_ <=> o.text_; }
    bool operator==(const Id& o) const { return text_ == o.text_; }

private:
    std::string text_;
    std::size_t prefixLen_ = 0;
    Uuid uuid_;
};

/// Crockford base32 (TypeID 변형) 인코딩/디코딩. 26자 ↔ 128-bit.
std::string encodeBase32(const Uuid& uuid);
std::optional<Uuid> decodeBase32(std::string_view suffix);

/// UUIDv7 (RFC 9562 §5.7). 호출마다 단조 증가한다 (같은 ms 안에서는 counter 증가).
Uuid makeUuidV7();
/// UUIDv8 (RFC 9562 §5.8) — 임의의 128-bit 값에 version/variant 비트만 설정.
Uuid makeUuidV8(std::uint64_t hi, std::uint64_t lo);

} // namespace pme

template <> struct std::hash<pme::Id> {
    std::size_t operator()(const pme::Id& id) const noexcept { return std::hash<std::string>{}(id.str()); }
};
