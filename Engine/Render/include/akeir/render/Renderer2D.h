// akeir/render/Renderer2D.h — 2D 스프라이트(placeholder) 렌더러 + PNG capture. 설계 문서 §27 (capture 는 1급 기능), §27.1 (golden 비교), §20 (headless 에서도 capture).
//
//   두 가지 타깃:
//     createForWindow(SDL_Window*)  — 창에 그린다 (가속 SDL_Renderer). `akeir run` (창 모드)
//     createSoftware(w, h)          — SDL_Surface 에 CPU 로 그린다 (SDL_CreateSoftwareRenderer). **창도 GPU 도 필요 없다** → `akeir capture`, 테스트의 capture assertion.
//                                     software rasterizer 이므로 같은 입력이면 같은 PNG 바이트 (golden 테스트의 전제, §27.1). ADR-0026.
//   그리는 것 (Phase 2 PoC): SpriteRenderer 가 있는 entity 를 Transform 위치/스케일에 tint 색 사각형(또는 Collider2D 모양 크기)으로. 텍스처(Ref sprite)는 아직 로드하지 않는다.
//   카메라: Camera2D.primary 인 entity 의 Transform.position 을 중심으로 orthoSize(세로 반높이, world 단위). 없으면 원점·10.
//   Y 축은 위가 + (물리 좌표), 화면은 아래가 + 이므로 뒤집는다.
#pragma once

#include "akeir/core/Json.h"
#include "akeir/ecs/PlayWorld.h"
#include "akeir/runtime/Assets.h"

#include <memory>
#include <optional>
#include <map>
#include <set>
#include <string>
#include <utility>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Surface;
struct SDL_Texture;

namespace akeir {

struct FontFace;
class GlyphAtlas;

struct RenderStats {
    int sprites = 0;
    int texts = 0;      // TextRenderer entities drawn (ADR-0040)
    int culled = 0;     // sprites/texts skipped because their screen rect is outside the viewport (ADR-0044)
    double renderMs = 0;   // wall-clock of render() (profiling only — never feeds the simulation)
    int glyphs = 0;        // TTF glyphs drawn this frame (ADR-0046)
    int width = 0, height = 0;
    std::string backend;           // SDL renderer 이름 ("software", "direct3d11", …)
    Json camera;                   // {entity, position, orthoSize}
};

class Renderer2D {
public:
    static std::unique_ptr<Renderer2D> createForWindow(SDL_Window* window, std::string* error = nullptr);
    static std::unique_ptr<Renderer2D> createSoftware(int width, int height, std::string* error = nullptr);
    ~Renderer2D();
    Renderer2D(const Renderer2D&) = delete;
    Renderer2D& operator=(const Renderer2D&) = delete;

    /// world 를 그린다 (clear 포함). present 는 하지 않는다.
    RenderStats render(const PlayWorld& world);
    void present();
    /// 마지막 render 결과를 PNG 로. software 타깃은 surface 를 바로, 창 타깃은 SDL_RenderReadPixels.
    bool savePng(const std::string& path, std::string* error = nullptr);
    /// RGBA8 픽셀 (테스트/비교용). 실패 시 빈 벡터.
    std::vector<std::uint8_t> readPixels(int* width, int* height);

    int width() const { return width_; }
    int height() const { return height_; }
    std::string backendName() const;

private:
    Renderer2D() = default;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Surface* surface_ = nullptr;
    // texture cache per asset id (ADR-0037). nullptr = load failed (warned once); textures are owned by this renderer
    std::map<std::string, SDL_Texture*> textures_;
    std::set<std::string> warnedAssets_;
    // ADR-0046: TTF faces per asset id and glyph atlases per (asset id, pixel height); owned here, built on first use
    std::map<std::string, std::unique_ptr<FontFace>> fonts_;
    std::map<std::pair<std::string, int>, std::unique_ptr<GlyphAtlas>> atlases_;
    GlyphAtlas* atlasFor(const AssetMeta& asset, int pixelHeight);
    SDL_Texture* textureFor(const AssetMeta& asset);   // software 타깃일 때 소유
    int width_ = 0, height_ = 0;
};

/// §27.1 golden 비교 (pixelmatch 식 단순화: 채널 최대 차이가 perPixel(0~1) 을 넘으면 mismatch; 비율이 maxMismatchRatio 를 넘으면 실패).
struct CaptureTolerance {
    double perPixel = 0.1;
    double maxMismatchRatio = 0.002;
};
struct CaptureCompareResult {
    bool ok = false;
    bool sizeMismatch = false;
    int width = 0, height = 0;
    long long mismatchedPixels = 0;
    double ratio = 0;
    std::string error;
    Json toJson() const;
};
/// 두 PNG 를 비교하고 (선택) diff PNG 를 쓴다 (mismatch 픽셀 = 빨강, 나머지는 expected 를 흐리게).
CaptureCompareResult compareCaptures(const std::string& expectedPng, const std::string& actualPng, const CaptureTolerance& tol = {}, const std::string& diffPngOut = "");

} // namespace akeir
