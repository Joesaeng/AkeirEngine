// pme/core/Ref.h — persistent id 참조 leaf. 설계 문서 §7.4 (파일 안의 참조는 항상 id), §19 (sub-asset: asset_…#<kind>/<name>), §42.2 (refType).
//
//   JSON 표현: "entity_01j5…", "prefab_01j5…", "asset_01j5…#sprites/goblin_idle". 빈 문자열 = 참조 없음.
#pragma once

#include "pme/core/Id.h"

#include <optional>
#include <string>
#include <string_view>

namespace pme {

struct Ref {
    std::string value;   // "<id>" 또는 "<id>#<sub>"

    bool empty() const { return value.empty(); }
    std::string_view idPart() const { auto h = value.find('#'); return std::string_view(value).substr(0, h); }
    std::string_view subPart() const { auto h = value.find('#'); return h == std::string::npos ? std::string_view{} : std::string_view(value).substr(h + 1); }
    std::optional<Id> id() const { return Id::parse(idPart()); }
    /// 형식 검사: 빈 값이거나 유효한 id (+ 선택적 #sub). 오류면 이유.
    static std::string validate(std::string_view text) {
        if (text.empty()) return {};
        auto h = text.find('#');
        std::string err = Id::validate(text.substr(0, h));
        if (!err.empty()) return "ref id part: " + err;
        if (h != std::string_view::npos && h + 1 >= text.size()) return "ref has '#' but empty sub-asset path";
        return {};
    }
    bool operator==(const Ref&) const = default;
};

} // namespace pme
