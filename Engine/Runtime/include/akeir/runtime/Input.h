// akeir/runtime/Input.h — tick 단위 입력 프레임. 설계 문서 §88.3 (입력은 데이터), §22.2 (Input / Replay), §8.2 (play 중 command 도 입력).
//
//   sim 은 device 입력을 절대 직접 읽지 않는다. 매 tick 하나의 InputFrame 만 받는다.
//   InputFrame = { tick, actions: { "MoveX": 1.0, "Attack": 1.0 }, commands: [ {op, args}… ] }
//   device → action 변환은 sim 밖(platform layer, Config/input.json 의 action map)에서 일어난다.
//   replay(§22.3) 의 inputs.jsonl 한 줄이 InputFrame 하나다.
#pragma once

#include "akeir/core/Json.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace akeir {

struct InputFrame {
    std::int64_t tick = 0;
    std::map<std::string, float> actions;   // 정렬된 맵 — 순회 순서가 결정적 (§22.2)
    std::vector<Json> commands;             // play 중 들어온 command (tick-stamped, §8.2)

    float axis(const std::string& name) const { auto it = actions.find(name); return it == actions.end() ? 0.0f : it->second; }
    bool pressed(const std::string& name) const { return axis(name) > 0.5f; }

    Json toJson() const;
    static InputFrame fromJson(const Json& j);
};

} // namespace akeir
