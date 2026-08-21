// akeir/platform/InputMap.h — Config/input.json 의 action map → SDL 키보드 상태 → InputFrame. 설계 문서 §88.3 (입력 = action 값 맵, sim 은 키를 모른다), §22.3 (replay 는 InputFrame 단위).
//
//   input.json:
//     { "actions": { "MoveX": { "type": "axis",   "bindings": [ { "keys": ["A", "D"], "scale": [-1, 1] }, { "gamepad": "leftStickX" } ] },
//                    "Attack": { "type": "button", "bindings": [ { "key": "Space" }, { "mouse": "left" } ] } } }
//   키 이름은 SDL_GetScancodeFromName 이 아는 이름 ("A", "Left", "Space", "Up" …). gamepad / mouse 바인딩은 이 PoC 에서 무시한다 (파싱은 하고 `unsupported` 로 보고).
//   axis: 눌린 키들의 scale 합을 [-1, 1] 로 clamp. button: 눌려 있으면 1.0. 값이 0 인 action 은 InputFrame 에 넣지 않는다 (replay 파일을 작게).
#pragma once

#include "akeir/core/Diagnostic.h"
#include "akeir/core/Json.h"
#include "akeir/runtime/Input.h"

#include <string>
#include <vector>

namespace akeir {

struct InputBinding {
    std::vector<int> scancodes;   // SDL_Scancode 값
    std::vector<float> scales;    // scancodes 와 같은 길이 (button 은 1.0)
};

struct InputAction {
    std::string name;
    bool axis = false;
    std::vector<InputBinding> bindings;
    std::vector<std::string> unsupported;   // "gamepad:leftStickX", "mouse:left"
};

class InputMap {
public:
    /// input.json 파싱 (SDL 이 초기화되어 있어야 scancode 이름 변환이 된다). 문제는 diagnostics 로.
    static InputMap fromJson(const Json& doc, std::vector<Diagnostic>* diagnostics = nullptr);
    static InputMap loadFile(const std::string& path, std::vector<Diagnostic>* diagnostics = nullptr);

    /// 현재 SDL 키보드 상태로 InputFrame 을 만든다. SDL_PumpEvents 는 호출자가(Platform::pumpEvents).
    InputFrame sample(std::int64_t tick) const;
    const std::vector<InputAction>& actions() const { return actions_; }
    Json toJson() const;   // 디버그/`akeir input map` 용

private:
    std::vector<InputAction> actions_;
};

} // namespace akeir
