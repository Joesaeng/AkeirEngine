// Renderer2D.cpp — SDL_Renderer 기반 placeholder 스프라이트 렌더 + PNG capture + golden 비교 (§27, §27.1)
#include "akeir/render/Renderer2D.h"

#include "Font5x7.h"
#include "akeir/core/Log.h"
#include "akeir/runtime/Components.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace akeir {

std::unique_ptr<Renderer2D> Renderer2D::createForWindow(SDL_Window* window, std::string* error) {
    std::unique_ptr<Renderer2D> r(new Renderer2D());
    r->renderer_ = SDL_CreateRenderer(window, nullptr);
    if (!r->renderer_) { if (error) *error = std::string("SDL_CreateRenderer failed: ") + SDL_GetError(); return nullptr; }
    SDL_SetRenderVSync(r->renderer_, 1);   // 창 모드는 vsync 로 프레임을 묶는다 (sim tick 은 accumulator 가 정한다, §20.1)
    SDL_GetWindowSizeInPixels(window, &r->width_, &r->height_);
    return r;
}

std::unique_ptr<Renderer2D> Renderer2D::createSoftware(int width, int height, std::string* error) {
    std::unique_ptr<Renderer2D> r(new Renderer2D());
    r->surface_ = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
    if (!r->surface_) { if (error) *error = std::string("SDL_CreateSurface failed: ") + SDL_GetError(); return nullptr; }
    r->renderer_ = SDL_CreateSoftwareRenderer(r->surface_);
    if (!r->renderer_) { if (error) *error = std::string("SDL_CreateSoftwareRenderer failed: ") + SDL_GetError(); return nullptr; }
    r->width_ = width;
    r->height_ = height;
    return r;
}

Renderer2D::~Renderer2D() {
    for (auto& [id, tex] : textures_) if (tex) SDL_DestroyTexture(tex);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (surface_) SDL_DestroySurface(surface_);
}

std::string Renderer2D::backendName() const {
    const char* n = renderer_ ? SDL_GetRendererName(renderer_) : nullptr;
    return n ? n : "";
}

namespace {
Uint8 toByte(float c) { return static_cast<Uint8>(std::clamp(static_cast<int>(std::lround(c * 255.f)), 0, 255)); }
} // namespace

RenderStats Renderer2D::render(const PlayWorld& world) {
    RenderStats stats;
    stats.width = width_;
    stats.height = height_;
    stats.backend = backendName();
    if (surface_ == nullptr) SDL_GetCurrentRenderOutputSize(renderer_, &width_, &height_);

    // 카메라
    Vec3 camPos{};
    float ortho = 10.f;
    Color bg{0.1f, 0.1f, 0.12f, 1.f};
    std::string camEntity;
    for (const auto& id : world.entityIds()) {
        const Camera2D* cam = world.get<Camera2D>(id);
        if (!cam || !cam->primary) continue;
        if (const Transform* t = world.get<Transform>(id)) camPos = t->position;
        ortho = cam->orthoSize > 0.01f ? cam->orthoSize : 10.f;
        bg = cam->background;
        camEntity = id;
        break;
    }
    stats.camera = Json{{"entity", camEntity}, {"position", Json::array({camPos.x, camPos.y})}, {"orthoSize", ortho}};
    const float ppu = static_cast<float>(height_) / (2.f * ortho);   // pixels per world unit
    const float cx = width_ * 0.5f, cy = height_ * 0.5f;

    SDL_SetRenderDrawColor(renderer_, toByte(bg.r), toByte(bg.g), toByte(bg.b), 255);
    SDL_RenderClear(renderer_);

    struct Item { int order; std::string id; SDL_FRect rect; Color color; bool circle; SDL_Texture* tex; SDL_FRect src; bool flipX, flipY; };
    std::vector<Item> items;
    for (const auto& id : world.entityIds()) {
        const SpriteRenderer* sp = world.get<SpriteRenderer>(id);
        const Transform* t = world.get<Transform>(id);
        if (!sp || !t) continue;
        float w = 1.f, h = 1.f;
        bool circle = false;
        SDL_Texture* tex = nullptr;
        SDL_FRect src{};
        Vec2 pivot{0.5f, 0.5f};
        if (!sp->sprite.empty()) {
            // ADR-0037: "asset_…#sprites/<name>" → texture region. Unresolvable refs are validate errors; here we fall back to the shape
            const AssetMeta* asset = nullptr;
            std::string why;
            if (const SpriteRegion* reg = world.assets().resolveSprite(sp->sprite, &asset, &why)) {
                if (SDL_Texture* loaded = textureFor(*asset)) {
                    tex = loaded;
                    src = SDL_FRect{static_cast<float>(reg->x), static_cast<float>(reg->y), static_cast<float>(reg->w), static_cast<float>(reg->h)};
                    w = static_cast<float>(reg->w) / asset->pixelsPerUnit;
                    h = static_cast<float>(reg->h) / asset->pixelsPerUnit;
                    pivot = reg->pivot;
                }
            } else if (warnedAssets_.insert(sp->sprite.value).second) {
                AKEIR_LOG(Warn, "render", "sprite_unresolved", "Sprite reference does not resolve; drawing the placeholder shape.", Json{{"game.entity", id}, {"sprite", sp->sprite.value}, {"why", why}});
            }
        }
        if (!tex) {
            if (const Collider2D* c = world.get<Collider2D>(id)) {
                if (c->shape == ColliderShape::Box) { w = c->size.x; h = c->size.y; }
                else { w = h = c->radius * 2.f; circle = c->shape == ColliderShape::Circle; }
            }
        }
        w *= t->scale.x; h *= t->scale.y;
        SDL_FRect r;
        r.w = std::max(1.f, w * ppu);
        r.h = std::max(1.f, h * ppu);
        r.x = cx + (t->position.x - camPos.x) * ppu - r.w * pivot.x;
        r.y = cy - (t->position.y - camPos.y) * ppu - r.h * (1.f - pivot.y);
        items.push_back({sp->sortingOrder, id, r, sp->tint, circle, tex, src, sp->flipX, sp->flipY});
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.order != b.order ? a.order < b.order : a.id < b.id; });
    for (const auto& it : items) {
        if (it.tex) {
            // tint multiplies the texture (white = unchanged); nearest filtering keeps pixel art crisp and the software path deterministic
            SDL_SetTextureColorMod(it.tex, toByte(it.color.r), toByte(it.color.g), toByte(it.color.b));
            SDL_SetTextureAlphaMod(it.tex, toByte(it.color.a));
            if (it.flipX || it.flipY) {
                SDL_FlipMode flip = static_cast<SDL_FlipMode>((it.flipX ? SDL_FLIP_HORIZONTAL : 0) | (it.flipY ? SDL_FLIP_VERTICAL : 0));
                SDL_RenderTextureRotated(renderer_, it.tex, &it.src, &it.rect, 0.0, nullptr, flip);
            } else {
                SDL_RenderTexture(renderer_, it.tex, &it.src, &it.rect);
            }
            ++stats.sprites;
            continue;
        }
        SDL_SetRenderDrawColor(renderer_, toByte(it.color.r), toByte(it.color.g), toByte(it.color.b), toByte(it.color.a));
        if (it.circle) {
            // 원: 가로 스캔라인 사각형으로 근사 (software renderer 에서도 결정적)
            const int rows = std::max(2, static_cast<int>(it.rect.h));
            const float rad = it.rect.h * 0.5f, ccx = it.rect.x + it.rect.w * 0.5f, ccy = it.rect.y + rad;
            for (int i = 0; i < rows; ++i) {
                float y = it.rect.y + i;
                float dy = (y + 0.5f) - ccy;
                float half = std::sqrt(std::max(0.f, rad * rad - dy * dy)) * (it.rect.w / it.rect.h);
                SDL_FRect line{ccx - half, y, half * 2.f, 1.f};
                SDL_RenderFillRect(renderer_, &line);
            }
        } else {
            SDL_RenderFillRect(renderer_, &it.rect);
        }
        ++stats.sprites;
    }
    // text (ADR-0040): after sprites, in sortingOrder then id order. Glyph pixels are filled rects → deterministic on the software path.
    struct TextItem { int order; std::string id; const TextRenderer* tr; const Transform* t; };
    std::vector<TextItem> texts;
    for (const auto& id : world.entityIds()) {
        const TextRenderer* tr = world.get<TextRenderer>(id);
        const Transform* t = world.get<Transform>(id);
        if (tr && t && !tr->text.empty()) texts.push_back({tr->sortingOrder, id, tr, t});
    }
    std::stable_sort(texts.begin(), texts.end(), [](const TextItem& a, const TextItem& b) { return a.order != b.order ? a.order < b.order : a.id < b.id; });
    for (const auto& it : texts) {
        const float px = std::max(0.25f, it.tr->scale);
        const float advance = font5x7::kAdvance * px, glyphH = font5x7::kHeight * px;
        // decode UTF-8 into code points; unsupported ones draw as the box glyph
        std::vector<unsigned int> cps;
        for (std::size_t i = 0; i < it.tr->text.size();) {
            unsigned char c = static_cast<unsigned char>(it.tr->text[i]);
            unsigned int cp = c; std::size_t n = 1;
            if (c >= 0xF0) { cp = c & 0x07; n = 4; } else if (c >= 0xE0) { cp = c & 0x0F; n = 3; } else if (c >= 0xC0) { cp = c & 0x1F; n = 2; }
            for (std::size_t k = 1; k < n && i + k < it.tr->text.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(it.tr->text[i + k]) & 0x3F);
            cps.push_back(cp);
            i += n;
        }
        const float totalW = cps.empty() ? 0.f : advance * cps.size() - px;
        float x0, y0;
        if (it.tr->screenSpace) { x0 = it.t->position.x; y0 = it.t->position.y; }
        else { x0 = cx + (it.t->position.x - camPos.x) * ppu; y0 = cy - (it.t->position.y - camPos.y) * ppu - glyphH * 0.5f; }
        if (it.tr->align == TextAlign::Center) x0 -= totalW * 0.5f; else if (it.tr->align == TextAlign::Right) x0 -= totalW;
        x0 = std::floor(x0); y0 = std::floor(y0);
        SDL_SetRenderDrawColor(renderer_, toByte(it.tr->color.r), toByte(it.tr->color.g), toByte(it.tr->color.b), toByte(it.tr->color.a));
        for (std::size_t ci = 0; ci < cps.size(); ++ci) {
            const font5x7::Glyph& g = font5x7::glyph(cps[ci]);
            for (int row = 0; row < font5x7::kHeight; ++row)
                for (int col = 0; col < font5x7::kWidth; ++col)
                    if (g.rows[row] & (1 << (font5x7::kWidth - 1 - col))) {
                        SDL_FRect r{x0 + ci * advance + col * px, y0 + row * px, px, px};
                        SDL_RenderFillRect(renderer_, &r);
                    }
        }
        ++stats.texts;
    }
    SDL_FlushRenderer(renderer_);   // software renderer 는 배치를 flush 해야 surface 에 픽셀이 있다 (readPixels/savePng 전에 필수)
    return stats;
}

SDL_Texture* Renderer2D::textureFor(const AssetMeta& asset) {
    auto it = textures_.find(asset.id);
    if (it != textures_.end()) return it->second;
    SDL_Texture* tex = nullptr;
    if (SDL_Surface* surf = SDL_LoadPNG(asset.sourceAbs.c_str())) {
        tex = SDL_CreateTextureFromSurface(renderer_, surf);
        SDL_DestroySurface(surf);
        if (tex) SDL_SetTextureScaleMode(tex, asset.filter == "linear" ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    }
    if (!tex) AKEIR_LOG(Warn, "render", "texture_load_failed", "Cannot load texture; entities using it draw as placeholder shapes.", Json{{"asset", asset.id}, {"source", asset.sourceAbs}, {"error", SDL_GetError()}});
    textures_[asset.id] = tex;   // nullptr is cached too: warn once, not every frame
    return tex;
}

void Renderer2D::present() {
    if (surface_ == nullptr) SDL_RenderPresent(renderer_);
    else SDL_FlushRenderer(renderer_);
}

std::vector<std::uint8_t> Renderer2D::readPixels(int* width, int* height) {
    std::vector<std::uint8_t> out;
    SDL_Surface* s = surface_;
    SDL_Surface* owned = nullptr;
    if (!s) { owned = SDL_RenderReadPixels(renderer_, nullptr); s = owned; }
    if (!s) return out;
    SDL_Surface* conv = s->format == SDL_PIXELFORMAT_RGBA32 ? nullptr : SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_Surface* src = conv ? conv : s;
    if (SDL_LockSurface(src)) {
        out.resize(static_cast<std::size_t>(src->w) * src->h * 4);
        for (int y = 0; y < src->h; ++y)
            std::copy_n(static_cast<const std::uint8_t*>(src->pixels) + static_cast<std::size_t>(y) * src->pitch, static_cast<std::size_t>(src->w) * 4, out.data() + static_cast<std::size_t>(y) * src->w * 4);
        SDL_UnlockSurface(src);
        if (width) *width = src->w;
        if (height) *height = src->h;
    }
    if (conv) SDL_DestroySurface(conv);
    if (owned) SDL_DestroySurface(owned);
    return out;
}

bool Renderer2D::savePng(const std::string& path, std::string* error) {
    SDL_Surface* s = surface_;
    SDL_Surface* owned = nullptr;
    if (!s) { owned = SDL_RenderReadPixels(renderer_, nullptr); s = owned; }
    if (!s) { if (error) *error = std::string("SDL_RenderReadPixels failed: ") + SDL_GetError(); return false; }
    bool ok = SDL_SavePNG(s, path.c_str());
    if (!ok && error) *error = std::string("SDL_SavePNG failed: ") + SDL_GetError();
    if (owned) SDL_DestroySurface(owned);
    return ok;
}

// ---------------------------------------------------------------- compare (§27.1)

Json CaptureCompareResult::toJson() const {
    Json j = Json{{"ok", ok}, {"width", width}, {"height", height}, {"mismatchedPixels", mismatchedPixels}, {"ratio", ratio}};
    if (sizeMismatch) j["sizeMismatch"] = true;
    if (!error.empty()) j["error"] = error;
    return j;
}

CaptureCompareResult compareCaptures(const std::string& expectedPng, const std::string& actualPng, const CaptureTolerance& tol, const std::string& diffPngOut) {
    CaptureCompareResult r;
    SDL_Surface* a = SDL_LoadPNG(expectedPng.c_str());
    if (!a) { r.error = "cannot load " + expectedPng + ": " + SDL_GetError(); return r; }
    SDL_Surface* b = SDL_LoadPNG(actualPng.c_str());
    if (!b) { r.error = "cannot load " + actualPng + ": " + SDL_GetError(); SDL_DestroySurface(a); return r; }
    SDL_Surface* ca = SDL_ConvertSurface(a, SDL_PIXELFORMAT_RGBA32);
    SDL_Surface* cb = SDL_ConvertSurface(b, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(a); SDL_DestroySurface(b);
    if (!ca || !cb) { r.error = "convert failed"; if (ca) SDL_DestroySurface(ca); if (cb) SDL_DestroySurface(cb); return r; }
    r.width = ca->w; r.height = ca->h;
    if (ca->w != cb->w || ca->h != cb->h) {
        r.sizeMismatch = true;
        r.error = "size differs: expected " + std::to_string(ca->w) + "x" + std::to_string(ca->h) + ", actual " + std::to_string(cb->w) + "x" + std::to_string(cb->h);
        SDL_DestroySurface(ca); SDL_DestroySurface(cb);
        return r;
    }
    SDL_Surface* diff = diffPngOut.empty() ? nullptr : SDL_CreateSurface(ca->w, ca->h, SDL_PIXELFORMAT_RGBA32);
    const int threshold = static_cast<int>(std::lround(tol.perPixel * 255.0));
    SDL_LockSurface(ca); SDL_LockSurface(cb); if (diff) SDL_LockSurface(diff);
    for (int y = 0; y < ca->h; ++y) {
        const Uint8* pa = static_cast<const Uint8*>(ca->pixels) + static_cast<std::size_t>(y) * ca->pitch;
        const Uint8* pb = static_cast<const Uint8*>(cb->pixels) + static_cast<std::size_t>(y) * cb->pitch;
        Uint8* pd = diff ? static_cast<Uint8*>(diff->pixels) + static_cast<std::size_t>(y) * diff->pitch : nullptr;
        for (int x = 0; x < ca->w; ++x) {
            int d = 0;
            for (int c = 0; c < 4; ++c) d = std::max(d, std::abs(static_cast<int>(pa[x * 4 + c]) - static_cast<int>(pb[x * 4 + c])));
            bool mis = d > threshold;
            if (mis) ++r.mismatchedPixels;
            if (pd) {
                if (mis) { pd[x * 4] = 255; pd[x * 4 + 1] = 0; pd[x * 4 + 2] = 0; pd[x * 4 + 3] = 255; }
                else { for (int c = 0; c < 3; ++c) pd[x * 4 + c] = static_cast<Uint8>(128 + pa[x * 4 + c] / 2); pd[x * 4 + 3] = 255; }
            }
        }
    }
    SDL_UnlockSurface(ca); SDL_UnlockSurface(cb); if (diff) SDL_UnlockSurface(diff);
    r.ratio = static_cast<double>(r.mismatchedPixels) / (static_cast<double>(ca->w) * ca->h);
    r.ok = r.ratio <= tol.maxMismatchRatio;
    if (diff) { if (!SDL_SavePNG(diff, diffPngOut.c_str())) r.error = std::string("diff save failed: ") + SDL_GetError(); SDL_DestroySurface(diff); }
    SDL_DestroySurface(ca); SDL_DestroySurface(cb);
    return r;
}

} // namespace akeir
