// Platform.cpp — SDL3 init / window / events (§20)
#include "pme/platform/Platform.h"

#include "pme/core/Log.h"

#include <SDL3/SDL.h>

namespace pme {

std::unique_ptr<Platform> Platform::init(const PlatformConfig& cfg, std::string* error) {
    if (!cfg.videoDriver.empty()) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, cfg.videoDriver.c_str());
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");   // 오디오는 아직 없다 (§20)
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        if (error) *error = std::string("SDL_Init failed: ") + SDL_GetError();
        return nullptr;
    }
    std::unique_ptr<Platform> p(new Platform());
    p->width_ = cfg.width;
    p->height_ = cfg.height;
    if (cfg.window) {
        p->window_ = SDL_CreateWindow(cfg.title.c_str(), cfg.width, cfg.height, SDL_WINDOW_RESIZABLE);
        if (!p->window_) {
            if (error) *error = std::string("SDL_CreateWindow failed: ") + SDL_GetError();
            return nullptr;
        }
    }
    PME_LOG(Info, "platform", "sdl_init", "SDL initialized.", Json{{"videoDriver", p->currentVideoDriver()}, {"window", cfg.window}});
    return p;
}

Platform::~Platform() {
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

std::string Platform::currentVideoDriver() const {
    const char* d = SDL_GetCurrentVideoDriver();
    return d ? d : "";
}

bool Platform::pumpEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) quit_ = true;
        else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_ESCAPE) quit_ = true;
        else if (e.type == SDL_EVENT_WINDOW_RESIZED) { width_ = e.window.data1; height_ = e.window.data2; }
    }
    return !quit_;
}

} // namespace pme
