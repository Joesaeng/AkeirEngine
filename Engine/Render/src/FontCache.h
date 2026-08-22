// FontCache.h — TTF/OTF faces + glyph atlases for TextRenderer.font (ADR-0046). Private to akeir_render.
//
//   FontFace  = one font file parsed by stb_truetype (kept in memory; never re-read).
//   GlyphAtlas = one (face, pixel height) pair: glyphs are rasterized on first use (CPU, stb) into 8-bit coverage,
//   uploaded as white RGBA with alpha into shelf-packed SDL texture pages, and drawn with the text color as color mod.
//   Everything is CPU-side and ordered by first use → deterministic on the software renderer (same text → same pixels).
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct stbtt_fontinfo;

namespace akeir {

struct FontFace {
    std::vector<unsigned char> data;
    std::unique_ptr<stbtt_fontinfo> info;
    int ascent = 0, descent = 0, lineGap = 0;   // font units
    bool ok = false;
    std::string error;

    static std::unique_ptr<FontFace> load(const std::string& path);
    ~FontFace();
    FontFace();
};

struct Glyph {
    int page = -1;                  // -1 = empty glyph (space) or failed
    SDL_FRect src{};                // rect in the page
    int xoff = 0, yoff = 0;         // bitmap offset from the pen position (stb: yoff is relative to the baseline, negative = above)
    float advance = 0.f;            // pixels
};

class GlyphAtlas {
public:
    GlyphAtlas(SDL_Renderer* renderer, const FontFace& face, int pixelHeight);
    ~GlyphAtlas();
    GlyphAtlas(const GlyphAtlas&) = delete;
    GlyphAtlas& operator=(const GlyphAtlas&) = delete;

    const Glyph& glyph(unsigned int codepoint);     // rasterizes on first use
    float kerning(unsigned int a, unsigned int b) const;   // pixels
    float ascent() const { return ascentPx_; }
    float descent() const { return descentPx_; }   // negative
    float lineHeight() const { return lineHeightPx_; }
    SDL_Texture* page(int index) const { return pages_[static_cast<std::size_t>(index)]; }
    std::size_t glyphCount() const { return glyphs_.size(); }
    int pageCount() const { return static_cast<int>(pages_.size()); }

private:
    bool newPage();
    SDL_Renderer* renderer_;
    const FontFace& face_;
    float scale_ = 1.f;
    float ascentPx_ = 0.f, descentPx_ = 0.f, lineHeightPx_ = 0.f;
    std::map<unsigned int, Glyph> glyphs_;
    std::vector<SDL_Texture*> pages_;
    int pageSize_ = 512;
    int shelfY_ = 0, shelfH_ = 0, penX_ = 0;   // shelf packer state on the last page
};

/// Measured text run: pen positions are integers (floor) so glyphs stay crisp and layout is identical every frame.
struct TextLayout {
    float width = 0.f;
    std::vector<unsigned int> codepoints;
};
TextLayout layoutText(GlyphAtlas& atlas, const std::string& utf8);
std::vector<unsigned int> decodeUtf8(const std::string& s);

} // namespace akeir
