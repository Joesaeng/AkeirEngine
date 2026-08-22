// akeir/runtime/PhysicsLayers.h — collision layers declared in project.json (ADR-0043).
//
//   "physics": { "layers": { "Player": ["Enemy", "Pickup"], "Enemy": ["Player", "Enemy"], "Pickup": ["Player"], "Effect": [] } }
//
// Every Collider2D.layer must name a declared layer. A pair collides (and sensors report overlaps) when EITHER side
// lists the other — the matrix is symmetric by construction, so listing "Player: [Enemy]" is enough. Layer i gets
// category bit i (up to 64 layers); the mask is the OR of the partners' bits. No "physics.layers" in project.json
// = the pre-ADR-0043 behaviour: one layer, everything collides with everything.
#pragma once

#include "akeir/core/Json.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace akeir {

struct LayerBits { std::uint64_t category = 1; std::uint64_t mask = ~0ULL; };

class PhysicsLayers {
public:
    /// Parse "physics.layers". Problems (not an object, > 64 layers, partner not declared) go to `problems`
    /// as "<pointer>: <text>" and the offending entries are ignored.
    static PhysicsLayers fromProjectJson(const Json& projectJson, std::vector<std::pair<std::string, std::string>>* problems = nullptr);
    bool declared() const { return !names_.empty(); }
    bool has(std::string_view layer) const { return index_.count(std::string(layer)) != 0; }
    /// category/mask for a layer; undeclared → the collide-with-everything default (validate reports it)
    LayerBits bits(std::string_view layer) const;
    const std::vector<std::string>& names() const { return names_; }
    bool collides(std::string_view a, std::string_view b) const;
    Json toJson() const;
private:
    std::vector<std::string> names_;                 // declaration order → bit index
    std::map<std::string, std::size_t> index_;
    std::vector<std::uint64_t> masks_;
};

} // namespace akeir
