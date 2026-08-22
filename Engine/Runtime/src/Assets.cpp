// Assets.cpp — see akeir/runtime/Assets.h (ADR-0037)
#include "akeir/runtime/Assets.h"

#include <cstdint>
#include <fstream>

namespace akeir {

const SpriteRegion* AssetMeta::sprite(std::string_view name) const {
    for (const auto& s : sprites) if (s.name == name) return &s;
    return nullptr;
}

Json AssetMeta::toJson() const {
    Json subs = Json::array();
    for (const auto& s : sprites) subs.push_back(Json{{"name", s.name}, {"kind", "sprite"}, {"rect", Json::array({s.x, s.y, s.w, s.h})}, {"pivot", Json::array({s.pivot.x, s.pivot.y})}});
    return Json{{"id", id}, {"meta", metaPath}, {"source", sourceRel}, {"importer", importer}, {"settings", Json{{"filter", filter}, {"pixelsPerUnit", pixelsPerUnit}}}, {"subAssets", subs}};
}

bool pngDimensions(const std::string& path, int& width, int& height) {
    std::ifstream in(path, std::ios::binary);
    unsigned char h[24];
    if (!in.read(reinterpret_cast<char*>(h), sizeof h)) return false;
    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    for (int i = 0; i < 8; ++i) if (h[i] != sig[i]) return false;
    if (!(h[12] == 'I' && h[13] == 'H' && h[14] == 'D' && h[15] == 'R')) return false;
    auto be = [&](int o) { return (static_cast<std::uint32_t>(h[o]) << 24) | (static_cast<std::uint32_t>(h[o + 1]) << 16) | (static_cast<std::uint32_t>(h[o + 2]) << 8) | h[o + 3]; };
    width = static_cast<int>(be(16));
    height = static_cast<int>(be(20));
    return width > 0 && height > 0;
}

const AssetMeta* AssetTable::find(std::string_view id) const {
    auto it = byId_.find(id);
    return it == byId_.end() ? nullptr : &it->second;
}

bool fontFileSignature(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    unsigned char h[4];
    if (!in.read(reinterpret_cast<char*>(h), sizeof h)) return false;
    const bool ttf = h[0] == 0 && h[1] == 1 && h[2] == 0 && h[3] == 0;
    const bool mac = h[0] == 't' && h[1] == 'r' && h[2] == 'u' && h[3] == 'e';
    const bool otf = h[0] == 'O' && h[1] == 'T' && h[2] == 'T' && h[3] == 'O';
    const bool ttc = h[0] == 't' && h[1] == 't' && h[2] == 'c' && h[3] == 'f';
    return ttf || mac || otf || ttc;
}

const AssetMeta* AssetTable::resolveFont(const Ref& ref, std::string* why) const {
    if (ref.empty()) { if (why) *why = "empty reference"; return nullptr; }
    const AssetMeta* a = find(ref.idPart());
    if (!a) { if (why) *why = "no asset sidecar declares id " + std::string(ref.idPart()); return nullptr; }
    if (a->importer != "Font") { if (why) *why = "asset " + a->id + " is a " + a->importer + " asset, not a Font"; return nullptr; }
    if (!ref.subPart().empty()) { if (why) *why = "a font is referenced whole (no '#sub-asset')"; return nullptr; }
    if (why) why->clear();
    return a;
}

const SpriteRegion* AssetTable::resolveSprite(const Ref& ref, const AssetMeta** assetOut, std::string* why) const {
    if (assetOut) *assetOut = nullptr;
    if (ref.empty()) { if (why) *why = "empty reference"; return nullptr; }
    const AssetMeta* a = find(ref.idPart());
    if (!a) { if (why) *why = "no asset sidecar declares id " + std::string(ref.idPart()); return nullptr; }
    if (assetOut) *assetOut = a;
    std::string_view sub = ref.subPart();
    if (sub.rfind("sprites/", 0) != 0) { if (why) *why = "sub-asset address must be '#sprites/<name>' (got '#" + std::string(sub) + "')"; return nullptr; }
    const SpriteRegion* s = a->sprite(sub.substr(8));
    if (!s) {
        if (why) {
            std::string names;
            for (const auto& r : a->sprites) names += (names.empty() ? "" : ", ") + r.name;
            *why = "asset " + a->id + " has no sprite '" + std::string(sub.substr(8)) + "' (subAssets: " + names + ")";
        }
        return nullptr;
    }
    if (why) why->clear();
    return s;
}

} // namespace akeir
