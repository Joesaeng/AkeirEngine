// PhysicsLayers.cpp — see akeir/runtime/PhysicsLayers.h (ADR-0043)
#include "akeir/runtime/PhysicsLayers.h"

namespace akeir {

PhysicsLayers PhysicsLayers::fromProjectJson(const Json& pj, std::vector<std::pair<std::string, std::string>>* problems) {
    PhysicsLayers pl;
    auto bad = [&](const std::string& ptr, const std::string& text) { if (problems) problems->emplace_back(ptr, text); };
    if (!pj.is_object() || !pj.contains("physics")) return pl;
    const Json& ph = pj["physics"];
    if (!ph.is_object()) { bad("/physics", "physics must be an object"); return pl; }
    if (!ph.contains("layers")) return pl;
    const Json& layers = ph["layers"];
    if (!layers.is_object()) { bad("/physics/layers", "physics.layers must be an object {LayerName: [partner layers]}"); return pl; }
    for (auto it = layers.begin(); it != layers.end(); ++it) {
        if (pl.names_.size() >= 64) { bad("/physics/layers/" + it.key(), "at most 64 layers (Box2D category bits)"); break; }
        if (it.key().empty()) { bad("/physics/layers", "a layer needs a name"); continue; }
        pl.index_[it.key()] = pl.names_.size();
        pl.names_.push_back(it.key());
        pl.masks_.push_back(0);
    }
    for (auto it = layers.begin(); it != layers.end(); ++it) {
        auto me = pl.index_.find(it.key());
        if (me == pl.index_.end()) continue;
        if (!it.value().is_array()) { bad("/physics/layers/" + it.key(), "layer '" + it.key() + "' must list its partner layers as an array (empty = collides with nothing)"); continue; }
        for (const auto& p : it.value()) {
            if (!p.is_string()) { bad("/physics/layers/" + it.key(), "partner names must be strings"); continue; }
            auto other = pl.index_.find(p.get<std::string>());
            if (other == pl.index_.end()) { bad("/physics/layers/" + it.key(), "layer '" + it.key() + "' lists undeclared layer '" + p.get<std::string>() + "'"); continue; }
            pl.masks_[me->second] |= 1ULL << other->second;     // symmetric: either side listing the other makes the pair collide
            pl.masks_[other->second] |= 1ULL << me->second;
        }
    }
    return pl;
}

LayerBits PhysicsLayers::bits(std::string_view layer) const {
    auto it = index_.find(std::string(layer));
    if (it == index_.end()) return LayerBits{};
    return LayerBits{1ULL << it->second, masks_[it->second]};
}

bool PhysicsLayers::collides(std::string_view a, std::string_view b) const {
    if (!declared()) return true;
    LayerBits x = bits(a), y = bits(b);
    return (x.category & y.mask) != 0 && (y.category & x.mask) != 0;
}

Json PhysicsLayers::toJson() const {
    Json out = Json::object();
    for (std::size_t i = 0; i < names_.size(); ++i) {
        Json partners = Json::array();
        for (std::size_t j = 0; j < names_.size(); ++j) if (masks_[i] & (1ULL << j)) partners.push_back(names_[j]);
        out[names_[i]] = partners;
    }
    return out;
}

} // namespace akeir
