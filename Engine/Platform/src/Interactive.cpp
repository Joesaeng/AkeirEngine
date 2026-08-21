// Interactive.cpp — 창 모드 루프 (§20.1 accumulator 는 여기에만 있다)
#include "pme/platform/Interactive.h"

#include "pme/core/Hash.h"
#include "pme/core/Log.h"
#include "pme/core/Time.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <fstream>

namespace pme {

Json InteractiveResult::toJson() const {
    return Json{{"ticksRun", ticksRun}, {"finalHash", toHex64(finalHash)}, {"wallMs", wallMs}, {"frames", frames}, {"closedByUser", closedByUser}};
}

InteractiveResult runInteractive(Platform& platform, Renderer2D& renderer, PlayWorld& world, const InputMap& input, const InteractiveConfig& cfg) {
    InteractiveResult r;
    SimTime st;
    st.tickRate = cfg.tickRate;
    const double dt = 1.0 / cfg.tickRate;
    std::ofstream record;
    if (!cfg.recordInputsPath.empty()) record.open(cfg.recordInputsPath, std::ios::binary | std::ios::trunc);

    Stopwatch total;
    auto last = std::chrono::steady_clock::now();
    double acc = 0.0;
    auto& logger = Logger::global();
    while (platform.pumpEvents()) {
        auto now = std::chrono::steady_clock::now();
        double frame = std::chrono::duration<double>(now - last).count();
        last = now;
        acc += std::min(frame, cfg.maxFrameSeconds);
        while (acc >= dt) {
            if (cfg.maxTicks > 0 && st.tick >= cfg.maxTicks) break;
            logger.setCurrentTick(st.tick);
            InputFrame in = input.sample(st.tick);
            if (record) { Json j = in.toJson(); record << j.dump() << '\n'; }
            world.tick(in, st);
            st.advance();
            acc -= dt;
        }
        renderer.render(world);
        renderer.present();
        ++r.frames;
        if (cfg.maxTicks > 0 && st.tick >= cfg.maxTicks) break;
    }
    logger.setCurrentTick(-1);
    r.closedByUser = platform.quitRequested();
    r.ticksRun = st.tick;
    r.finalHash = world.hash();
    r.wallMs = total.elapsedMs();
    return r;
}

} // namespace pme
