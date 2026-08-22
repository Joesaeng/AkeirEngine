// InputMap.cpp — input.json → scancode 바인딩 → InputFrame (§88.3)
#include "akeir/platform/InputMap.h"

#include "akeir/platform/Platform.h"
#include "akeir/serialization/Canonical.h"

#include <SDL3/SDL.h>

#include <algorithm>

namespace akeir {

namespace {

bool scancodeOf(const std::string& name, int& out) {
    SDL_Scancode sc = SDL_GetScancodeFromName(name.c_str());
    if (sc == SDL_SCANCODE_UNKNOWN) return false;
    out = static_cast<int>(sc);
    return true;
}

} // namespace

InputMap InputMap::fromJson(const Json& doc, std::vector<Diagnostic>* diags) {
    InputMap m;
    auto warn = [&](const std::string& rule, const std::string& text, const std::string& ptr) {
        if (diags) diags->push_back(Diagnostic::warning(rule, text).in(PhysicalLocation{"Config/input.json", ptr, std::nullopt}));
    };
    if (!doc.is_object() || !doc.contains("actions") || !doc["actions"].is_object()) {
        if (diags) diags->push_back(Diagnostic::error("INPUT_MAP_INVALID", "input.json needs an 'actions' object.").in(PhysicalLocation{"Config/input.json", "", std::nullopt}));
        return m;
    }
    for (const auto& [name, a] : doc["actions"].items()) {
        InputAction act;
        act.name = name;
        act.axis = a.value("type", "button") == "axis";
        const std::string base = "/actions/" + name;
        if (!a.contains("bindings") || !a["bindings"].is_array()) { warn("INPUT_BINDING_MISSING", "Action '" + name + "' has no bindings.", base); m.actions_.push_back(act); continue; }
        std::size_t i = 0;
        for (const auto& b : a["bindings"]) {
            const std::string bptr = base + "/bindings/" + std::to_string(i++);
            if (b.contains("keys") && b["keys"].is_array()) {
                InputBinding bind;
                Json scales = b.value("scale", Json::array());
                std::size_t k = 0;
                for (const auto& key : b["keys"]) {
                    int sc = 0;
                    std::string keyName = key.is_string() ? key.get<std::string>() : "";
                    if (!scancodeOf(keyName, sc)) { warn("INPUT_KEY_UNKNOWN", "Unknown key name '" + keyName + "' (SDL scancode names: A, Left, Space, Up …).", bptr + "/keys/" + std::to_string(k)); ++k; continue; }
                    bind.scancodes.push_back(sc);
                    float s = act.axis ? (k < scales.size() && scales[k].is_number() ? scales[k].get<float>() : 1.0f) : 1.0f;
                    bind.scales.push_back(s);
                    ++k;
                }
                if (!bind.scancodes.empty()) act.bindings.push_back(std::move(bind));
            } else if (b.contains("key") && b["key"].is_string()) {
                int sc = 0;
                if (!scancodeOf(b["key"].get<std::string>(), sc)) { warn("INPUT_KEY_UNKNOWN", "Unknown key name '" + b["key"].get<std::string>() + "'.", bptr + "/key"); continue; }
                act.bindings.push_back(InputBinding{{sc}, {1.0f}});
            } else if (b.contains("gamepad")) act.unsupported.push_back("gamepad:" + b["gamepad"].get<std::string>());
            else if (b.contains("mouse") && b["mouse"].is_string()) {
                const std::uint32_t mask = pointerButtonMask(b["mouse"].get<std::string>());
                if (!mask) { warn("INPUT_MOUSE_UNKNOWN", "Unknown mouse button '" + b["mouse"].get<std::string>() + "' (left, middle, right, x1, x2).", bptr + "/mouse"); continue; }
                act.mouse.push_back(MouseBinding{mask, act.axis && b.contains("scale") && b["scale"].is_number() ? b["scale"].get<float>() : 1.f});
            }
            else warn("INPUT_BINDING_INVALID", "Binding needs 'keys', 'key', 'gamepad' or 'mouse'.", bptr);
        }
        m.actions_.push_back(std::move(act));
    }
    std::sort(m.actions_.begin(), m.actions_.end(), [](const InputAction& a, const InputAction& b) { return a.name < b.name; });
    return m;
}

InputMap InputMap::loadFile(const std::string& path, std::vector<Diagnostic>* diags) {
    std::string err;
    auto j = readJsonFile(path, &err);
    if (!j) {
        if (diags) diags->push_back(Diagnostic::error("INPUT_MAP_UNREADABLE", "Cannot read " + path + ": " + err).in(PhysicalLocation{path, "", std::nullopt}));
        return InputMap{};
    }
    return fromJson(*j, diags);
}

InputFrame InputMap::sample(std::int64_t tick, Platform* platform) {
    InputFrame f;
    f.tick = tick;
    int numKeys = 0;
    const bool* keys = SDL_GetKeyboardState(&numKeys);
    const bool focused = !platform || platform->focused();
    // pointer (ADR-0045): window pixels; SDL's button mask already uses left=1, middle=2, right=4, x1=8, x2=16
    float mx = 0.f, my = 0.f;
    const SDL_MouseButtonFlags mb = SDL_GetMouseState(&mx, &my);
    f.pointer.present = true;
    f.pointer.x = mx; f.pointer.y = my;
    f.pointer.buttons = focused ? static_cast<std::uint32_t>(mb) & 31u : 0u;
    f.pointer.inside = focused && (!platform || platform->pointerInside());
    if (platform) { f.pointer.viewportW = platform->width(); f.pointer.viewportH = platform->height(); f.pointer.wheel = platform->takeWheel(); }
    for (const auto& a : actions_) {
        float v = 0.f;
        if (focused) {   // losing focus releases every key and button (no stuck "MoveX" after alt-tab)
            if (keys) for (const auto& b : a.bindings)
                for (std::size_t i = 0; i < b.scancodes.size(); ++i)
                    if (b.scancodes[i] >= 0 && b.scancodes[i] < numKeys && keys[b.scancodes[i]]) v += b.scales[i];
            for (const auto& m : a.mouse) if (f.pointer.buttons & m.mask) v += m.scale;
        }
        if (a.axis) v = std::clamp(v, -1.f, 1.f); else v = v > 0 ? 1.f : 0.f;
        if (v != 0.f) f.actions[a.name] = v;
    }
    f = InputFrame::withEdges(std::move(f), hasPrev_ ? &prev_ : nullptr);
    prev_ = f;
    hasPrev_ = true;
    return f;
}

Json InputMap::toJson() const {
    Json arr = Json::array();
    for (const auto& a : actions_) {
        Json b = Json::array();
        for (const auto& x : a.bindings) {
            Json keys = Json::array();
            for (std::size_t i = 0; i < x.scancodes.size(); ++i) keys.push_back(Json{{"scancode", x.scancodes[i]}, {"name", SDL_GetScancodeName(static_cast<SDL_Scancode>(x.scancodes[i]))}, {"scale", x.scales[i]}});
            b.push_back(keys);
        }
        Json mouse = Json::array();
        for (const auto& m : a.mouse) mouse.push_back(Json{{"button", pointerButtonNames(m.mask)}, {"scale", m.scale}});
        arr.push_back(Json{{"name", a.name}, {"type", a.axis ? "axis" : "button"}, {"keys", b}, {"mouse", mouse}, {"unsupported", a.unsupported}});
    }
    return arr;
}

} // namespace akeir
