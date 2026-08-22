// akeir/runtime/Input.h — tick 단위 입력 프레임. 설계 문서 §88.3 (입력은 데이터), §22.2 (Input / Replay), §8.2 (play 중 command 도 입력), ADR-0045 (pointer, edges).
//
//   sim 은 device 입력을 절대 직접 읽지 않는다. 매 tick 하나의 InputFrame 만 받는다.
//   InputFrame = { tick, actions: { "MoveX": 1.0, "Attack": 1.0 }, pressed: ["Attack"], released: [], pointer: {…}, commands: [ {op, args}… ] }
//   device → action 변환은 sim 밖(platform layer, Config/input.json 의 action map)에서 일어난다.
//   replay(§22.3) 의 inputs.jsonl 한 줄이 InputFrame 하나다 — edge 와 pointer 도 기록되므로 replay 는 이 파일만 읽는다.
//
//   Edges (ADR-0045): "이번 tick 에 눌림/떼어짐" 은 연속한 두 frame 의 차이다. 한 곳(InputFrame::withEdges)에서만 계산하고,
//   InputMap(실제 장치)·test DSL·play step·replay 가 모두 그 결과를 frame 에 실어 보낸다. 게임 시스템은 이전 frame 을 기억할 필요가 없다.
#pragma once

#include "akeir/core/Json.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace akeir {

/// 포인터(마우스/터치) 상태. 창 픽셀 좌표(좌상단 원점, y 아래). viewport 는 그 tick 의 창 크기 — 게임은 이 값으로 anchor/hit-test 를 계산한다.
/// 버튼 비트는 SDL 순서: left=1, middle=2, right=4, x1=8, x2=16 (pointerButtonMask / pointerButtonNames).
struct PointerState {
    bool present = false;        // 이 tick 에 포인터 정보가 있다 (headless/test 에서 pointer 를 안 주면 false — 모든 값 0)
    float x = 0.f, y = 0.f;      // 창 픽셀
    int viewportW = 0, viewportH = 0;
    std::uint32_t buttons = 0;   // 지금 눌려 있는 버튼 mask
    std::uint32_t pressed = 0;   // 이번 tick 에 눌린 버튼 (edge)
    std::uint32_t released = 0;  // 이번 tick 에 떼어진 버튼 (edge)
    float wheel = 0.f;           // 이번 tick 의 세로 휠 (+ = 위/앞으로)
    bool inside = true;          // 포인터가 창 안에 있고 창이 포커스를 가진다 (false 면 buttons 는 0 — 포커스 잃음 = 전부 release)

    bool held(std::uint32_t mask) const { return (buttons & mask) != 0; }
    bool justPressed(std::uint32_t mask) const { return (pressed & mask) != 0; }
    bool justReleased(std::uint32_t mask) const { return (released & mask) != 0; }
    float nx() const { return viewportW > 0 ? x / static_cast<float>(viewportW) : 0.f; }   // 0..1 정규화 (viewport 모르면 0)
    float ny() const { return viewportH > 0 ? y / static_cast<float>(viewportH) : 0.f; }
};

constexpr std::uint32_t kPointerLeft = 1, kPointerMiddle = 2, kPointerRight = 4, kPointerX1 = 8, kPointerX2 = 16;
/// "left" | "middle" | "right" | "x1" | "x2" → mask (모르는 이름은 0)
std::uint32_t pointerButtonMask(const std::string& name);
std::vector<std::string> pointerButtonNames(std::uint32_t mask);   // mask → 이름 (비트 순)

struct InputFrame {
    std::int64_t tick = 0;
    std::map<std::string, float> actions;   // 정렬된 맵 — 순회 순서가 결정적 (§22.2). 값이 0 인 action 은 보통 생략된다.
    std::vector<std::string> pressedActions;    // 이번 tick 에 0 → 활성(> 0.5) 이 된 button/axis action (정렬)
    std::vector<std::string> releasedActions;   // 이번 tick 에 활성 → 0 이 된 action (정렬)
    PointerState pointer;
    std::vector<Json> commands;             // play 중 들어온 command (tick-stamped, §8.2)

    float axis(const std::string& name) const { auto it = actions.find(name); return it == actions.end() ? 0.0f : it->second; }
    bool held(const std::string& name) const { return axis(name) > 0.5f; }
    bool pressed(const std::string& name) const { return held(name); }   // 호환: "지금 눌려 있다" (edge 는 justPressed)
    bool justPressed(const std::string& name) const;
    bool justReleased(const std::string& name) const;

    /// edge 계산의 유일한 정의: prev(직전 tick 의 frame) 와 비교해 pressedActions/releasedActions/pointer.pressed/released 를 채운다.
    /// prev 가 없으면(첫 tick) 활성인 모든 action 이 pressed. inside=false 면 buttons 를 0 으로 본다.
    static InputFrame withEdges(InputFrame current, const InputFrame* prev);

    Json toJson() const;
    static InputFrame fromJson(const Json& j);
};

} // namespace akeir
