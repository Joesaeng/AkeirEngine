// akeir/platform/Platform.h — SDL3 초기화 / 창 / video driver 선택 / 이벤트 펌프. 설계 문서 §20 (dummy·offscreen driver), §88.3 (입력은 InputFrame 으로만 sim 에 들어간다).
//
//   videoDriver:
//     ""          플랫폼 기본 (Windows: windows) — 실제 창
//     "dummy"     창/GPU 없음. SDL_Init 은 되고 키보드 상태는 항상 비어 있다. `akeir run --headless` 는 SDL 을 아예 초기화하지 않으므로 이 값은 capture 의 software 경로용
//     "offscreen" EGL/GL 기반 offscreen — 이 PoC 는 쓰지 않는다 (software renderer 가 GPU 없이 결정적 capture 를 준다, ADR-0026)
//   실제 사용된 driver 는 currentVideoDriver() 로 보고한다 (§20: 결과 JSON 에 기록).
//   sim 은 Platform 을 모른다 — Platform 은 InputFrame 을 만들고(InputMap) 렌더러에 창을 빌려줄 뿐이다.
#pragma once

#include <memory>
#include <string>

struct SDL_Window;

namespace akeir {

struct PlatformConfig {
    std::string videoDriver;      // "" | "dummy" | "offscreen" (SDL_HINT_VIDEO_DRIVER; 쉼표 우선순위 목록 허용)
    bool window = true;           // false = SDL_Init(VIDEO) 만 (capture / 테스트)
    int width = 1280, height = 720;
    std::string title = "akeir";
};

class Platform {
public:
    /// SDL_Init + (선택) 창. 실패 시 nullptr + error.
    static std::unique_ptr<Platform> init(const PlatformConfig& cfg, std::string* error = nullptr);
    ~Platform();
    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;

    SDL_Window* window() const { return window_; }
    std::string currentVideoDriver() const;
    /// 이벤트 펌프. 창 닫기 / ESC 면 false.
    bool pumpEvents();
    bool quitRequested() const { return quit_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    Platform() = default;
    SDL_Window* window_ = nullptr;
    bool quit_ = false;
    int width_ = 0, height_ = 0;
};

} // namespace akeir
