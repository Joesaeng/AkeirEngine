// FontCache.cpp — stb_truetype rasterization + shelf-packed glyph atlases (ADR-0046)
#include "FontCache.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4267 4456 4457 4505 4701 4702 4996)
#endif
#include <stb_truetype.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace akeir {

FontFace::FontFace() = default;
FontFace::~FontFace() = default;

std::unique_ptr<FontFace> FontFace::load(const std::string& path) {
    auto f = std::make_unique<FontFace>();
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) { f->error = "cannot open " + path; return f; }
    std::fseek(fp, 0, SEEK_END);
    long n = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (n <= 0) { std::fclose(fp); f->error = "empty file " + path; return f; }
    f->data.resize(static_cast<std::size_t>(n));
    const std::size_t got = std::fread(f->data.data(), 1, f->data.size(), fp);
    std::fclose(fp);
    if (got != f->data.size()) { f->error = "short read " + path; return f; }
    f->info = std::make_unique<stbtt_fontinfo>();
    const int offset = stbtt_GetFontOffsetForIndex(f->data.data(), 0);
    if (offset < 0 || !stbtt_InitFont(f->info.get(), f->data.data(), offset)) { f->error = "not a TrueType/OpenType font: " + path; f->info.reset(); return f; }
    stbtt_GetFontVMetrics(f->info.get(), &f->ascent, &f->descent, &f->lineGap);
    f->ok = true;
    return f;
}

GlyphAtlas::GlyphAtlas(SDL_Renderer* renderer, const FontFace& face, int pixelHeight) : renderer_(renderer), face_(face) {
    scale_ = stbtt_ScaleForPixelHeight(face_.info.get(), static_cast<float>(pixelHeight));
    ascentPx_ = std::round(face_.ascent * scale_);
    descentPx_ = std::round(face_.descent * scale_);
    lineHeightPx_ = std::round((face_.ascent - face_.descent + face_.lineGap) * scale_);
    pageSize_ = std::clamp(pixelHeight * 16, 256, 2048);
}

GlyphAtlas::~GlyphAtlas() {
    for (SDL_Texture* t : pages_) if (t) SDL_DestroyTexture(t);
}

bool GlyphAtlas::newPage() {
    SDL_Texture* t = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, pageSize_, pageSize_);
    if (!t) return false;
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
    // clear to transparent white so color mod × alpha is all that matters
    std::vector<std::uint8_t> clear(static_cast<std::size_t>(pageSize_) * pageSize_ * 4, 0);
    for (std::size_t i = 0; i < clear.size(); i += 4) { clear[i] = clear[i + 1] = clear[i + 2] = 255; }
    SDL_UpdateTexture(t, nullptr, clear.data(), pageSize_ * 4);
    pages_.push_back(t);
    shelfY_ = 0; shelfH_ = 0; penX_ = 0;
    return true;
}

const Glyph& GlyphAtlas::glyph(unsigned int cp) {
    auto it = glyphs_.find(cp);
    if (it != glyphs_.end()) return it->second;
    Glyph g;
    const stbtt_fontinfo* info = face_.info.get();
    const int gi = stbtt_FindGlyphIndex(info, static_cast<int>(cp));   // 0 = the font's .notdef glyph: a visible "unknown" box, like the bitmap font
    int adv = 0, lsb = 0;
    stbtt_GetGlyphHMetrics(info, gi, &adv, &lsb);
    g.advance = std::round(adv * scale_);
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphBitmapBox(info, gi, scale_, scale_, &x0, &y0, &x1, &y1);
    const int w = x1 - x0, h = y1 - y0;
    if (w > 0 && h > 0) {
        std::vector<unsigned char> cov(static_cast<std::size_t>(w) * h);
        stbtt_MakeGlyphBitmap(info, cov.data(), w, h, w, scale_, scale_, gi);
        const int pw = w + 1, ph = h + 1;   // 1px padding right/bottom
        if (pages_.empty() || penX_ + pw > pageSize_) {
            if (!pages_.empty()) { shelfY_ += shelfH_; penX_ = 0; shelfH_ = 0; }
            if (pages_.empty() || shelfY_ + ph > pageSize_) { if (!newPage()) { glyphs_[cp] = g; return glyphs_[cp]; } }
        }
        if (shelfY_ + ph > pageSize_) { if (!newPage()) { glyphs_[cp] = g; return glyphs_[cp]; } }
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * h * 4);
        for (std::size_t i = 0; i < cov.size(); ++i) { rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255; rgba[i * 4 + 3] = cov[i]; }
        SDL_Rect dst{penX_, shelfY_, w, h};
        SDL_UpdateTexture(pages_.back(), &dst, rgba.data(), w * 4);
        g.page = static_cast<int>(pages_.size()) - 1;
        g.src = SDL_FRect{static_cast<float>(penX_), static_cast<float>(shelfY_), static_cast<float>(w), static_cast<float>(h)};
        g.xoff = x0; g.yoff = y0;
        penX_ += pw;
        shelfH_ = std::max(shelfH_, ph);
    }
    return glyphs_.emplace(cp, g).first->second;
}

float GlyphAtlas::kerning(unsigned int a, unsigned int b) const {
    return std::round(stbtt_GetCodepointKernAdvance(face_.info.get(), static_cast<int>(a), static_cast<int>(b)) * scale_);
}

std::vector<unsigned int> decodeUtf8(const std::string& s) {
    std::vector<unsigned int> cps;
    for (std::size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        unsigned int cp = c; std::size_t n = 1;
        if (c >= 0xF0) { cp = c & 0x07; n = 4; } else if (c >= 0xE0) { cp = c & 0x0F; n = 3; } else if (c >= 0xC0) { cp = c & 0x1F; n = 2; }
        for (std::size_t k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        cps.push_back(cp);
        i += n;
    }
    return cps;
}

TextLayout layoutText(GlyphAtlas& atlas, const std::string& utf8) {
    TextLayout l;
    l.codepoints = decodeUtf8(utf8);
    float pen = 0.f;
    for (std::size_t i = 0; i < l.codepoints.size(); ++i) {
        const Glyph& g = atlas.glyph(l.codepoints[i]);
        pen += g.advance;
        if (i + 1 < l.codepoints.size()) pen += atlas.kerning(l.codepoints[i], l.codepoints[i + 1]);
    }
    l.width = pen;
    return l;
}

} // namespace akeir
