// Interactive.cpp — 창 모드 루프 (§20.1 accumulator 는 여기에만 있다)
#include "akeir/platform/Interactive.h"

#include "akeir/core/Hash.h"
#include "akeir/core/Log.h"
#include "akeir/core/Time.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <vector>
#include <chrono>
#include <fstream>

namespace akeir {

Json InteractiveResult::toJson() const {
    return Json{{"ticksRun", ticksRun}, {"finalHash", toHex64(finalHash)}, {"wallMs", wallMs}, {"frames", frames}, {"closedByUser", closedByUser}, {"frameMsAvg", frameMsAvg}, {"frameMsP95", frameMsP95}, {"frameMsMax", frameMsMax}};
}

InteractiveResult runInteractive(Platform& platform, Renderer2D& renderer, PlayWorld& world, InputMap& input, const InteractiveConfig& cfg) {
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
    std::vector<double> frameMs;   // ADR-0044: frame-time distribution for the stop report (wall clock, never fed to the sim)
    while (platform.pumpEvents()) {
        auto now = std::chrono::steady_clock::now();
        double frame = std::chrono::duration<double>(now - last).count();
        last = now;
        if (r.frames > 0) frameMs.push_back(frame * 1000.0);
        acc += std::min(frame, cfg.maxFrameSeconds);
        while (acc >= dt) {
            if (cfg.maxTicks > 0 && st.tick >= cfg.maxTicks) break;
            logger.setCurrentTick(st.tick);
            InputFrame in = input.sample(st.tick, &platform);
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
    if (!frameMs.empty()) {
        std::vector<double> sorted = frameMs;
        std::sort(sorted.begin(), sorted.end());
        double sum = 0; for (double v : sorted) sum += v;
        r.frameMsAvg = sum / static_cast<double>(sorted.size());
        r.frameMsP95 = sorted[std::min(sorted.size() - 1, static_cast<std::size_t>(sorted.size() * 0.95))];
        r.frameMsMax = sorted.back();
    }
    return r;
}

} // namespace akeir
