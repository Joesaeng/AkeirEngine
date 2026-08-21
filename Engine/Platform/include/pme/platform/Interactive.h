// pme/platform/Interactive.h — 창 모드 실시간 루프 (fixed tick + accumulator + 매 프레임 렌더). 설계 문서 §20.1 (accumulator 는 render 쪽 문제), §22.3 (입력은 InputFrame 으로 기록 가능).
//
//   headless(`Application::runHeadless`)와 sim 경로는 같다: tick 마다 InputFrame 하나, dt 상수. 다른 것은 "언제 tick 하느냐"(wall clock) 와 렌더뿐이다.
//   그래서 창 모드에서 같은 입력 시퀀스를 기록해(--record) headless 로 replay 하면 같은 hash 가 나와야 한다.
#pragma once

#include "pme/core/Json.h"
#include "pme/ecs/PlayWorld.h"
#include "pme/platform/InputMap.h"
#include "pme/platform/Platform.h"
#include "pme/render/Renderer2D.h"

#include <cstdint>
#include <string>

namespace pme {

struct InteractiveConfig {
    std::int64_t maxTicks = 0;        // 0 = 창이 닫힐 때까지
    std::int32_t tickRate = 60;
    double maxFrameSeconds = 0.25;    // spiral of death 방지: 한 프레임에 이보다 많은 시간은 버린다
    std::string recordInputsPath;     // 비어 있지 않으면 tick 마다 InputFrame 을 JSONL 로 기록 (§22.3 replay 입력)
};

struct InteractiveResult {
    std::int64_t ticksRun = 0;
    std::uint64_t finalHash = 0;
    double wallMs = 0;
    int frames = 0;
    bool closedByUser = false;
    Json toJson() const;
};

InteractiveResult runInteractive(Platform& platform, Renderer2D& renderer, PlayWorld& world, const InputMap& input, const InteractiveConfig& cfg);

} // namespace pme
