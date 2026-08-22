// akeir/runtime/Assets.h — asset sidecars (§37) and sprite sub-assets (§88.7). ADR-0037.
//
//   Assets/Textures/arena.png               the source image (committed)
//   Assets/Textures/arena.png.meta.json     the sidecar that OWNS the id and the sub-asset names:
//   {
//     "$schema": "game://schema/asset-meta/1", "schemaVersion": 1,
//     "id": "asset_01j…", "source": "Assets/Textures/arena.png", "importer": "Texture2D",
//     "settings": { "filter": "nearest", "pixelsPerUnit": 16 },
//     "subAssets": [ { "name": "player", "kind": "sprite", "rect": [0, 0, 16, 16], "pivot": [0.5, 0.5] }, … ]
//   }
//   A component property of type Ref with refType "asset:texture" points at one sub-asset: "asset_01j…#sprites/player"
//   (kind "sprite" → address segment "sprites"). The renderer turns that into a texture region; `akeir validate`
//   reports ASSET_* rules (missing source file, bad rect, unknown sub-asset) so a broken reference never draws
//   silently as a placeholder shape.
//
//   The table is read-only authoring data in v1 (no asset.import command yet): edit the sidecar by hand and run
//   `akeir validate`. PlayWorld carries a copy so renderers/game code can resolve refs without the Project.
#pragma once

#include "akeir/core/Json.h"
#include "akeir/core/Math.h"
#include "akeir/core/Ref.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace akeir {

struct SpriteRegion {
    std::string name;
    int x = 0, y = 0, w = 0, h = 0;   // pixels in the source image
    Vec2 pivot{0.5f, 0.5f};           // 0..1 inside the rect; (0.5, 0.5) = centered on Transform.position
};

struct AssetMeta {
    std::string id;                   // asset_…
    std::string metaPath;             // project-relative sidecar path ("Assets/Textures/arena.png.meta.json")
    std::string sourceRel;            // project-relative source ("Assets/Textures/arena.png")
    std::string sourceAbs;            // absolute path for loaders
    std::string importer = "Texture2D";   // Texture2D | Font (ADR-0046: TTF/OTF, referenced whole by TextRenderer.font)
    std::string filter = "nearest";   // nearest | linear
    float pixelsPerUnit = 16.f;       // world units = pixels / pixelsPerUnit
    std::vector<SpriteRegion> sprites;
    const SpriteRegion* sprite(std::string_view name) const;
    Json toJson() const;
};

/// Width/height from a PNG's IHDR (no decoding). false when the file is not a readable PNG.
bool pngDimensions(const std::string& path, int& width, int& height);
/// true when the file starts with a TrueType/OpenType/TTC signature (0x00010000, 'true', 'OTTO', 'ttcf') — no parsing (ADR-0046).
bool fontFileSignature(const std::string& path);

/// id → AssetMeta. Deterministic order (std::map).
class AssetTable {
public:
    void add(AssetMeta meta) { byId_[meta.id] = std::move(meta); }
    const AssetMeta* find(std::string_view id) const;
    /// "asset_…#sprites/<name>" → (asset, sprite). Either may be null; `why` explains (empty when found).
    const SpriteRegion* resolveSprite(const Ref& ref, const AssetMeta** assetOut = nullptr, std::string* why = nullptr) const;
    /// "asset_…" (no sub-asset) → Font asset. null + `why` otherwise (ADR-0046).
    const AssetMeta* resolveFont(const Ref& ref, std::string* why = nullptr) const;
    const std::map<std::string, AssetMeta, std::less<>>& all() const { return byId_; }
    std::size_t size() const { return byId_.size(); }
    bool empty() const { return byId_.empty(); }
private:
    std::map<std::string, AssetMeta, std::less<>> byId_;
};

} // namespace akeir
