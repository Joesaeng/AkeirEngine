// akeir/reflection/Aggregate.h — compile-time member count of an aggregate struct (ADR-0035).
//
//   static_assert(akeir::aggregateArity<Transform>() == 3);
//
// Used by ComponentBuilder to detect members that were neither reflected (AKEIR_PROP) nor explicitly
// excluded (AKEIR_SKIP): such a member is invisible to JSON authoring, validate, set/undo, query,
// snapshot and finalHash, silently — the "hidden metadata contract" PRINCIPLES §6 / §26 warns about.
//
// Technique (Boost.PFR style): `T{Any, Any, …}` is well-formed for exactly 0 … N initializers where N is
// the number of members, because `Any` converts to *any* member type. A nested aggregate member (Vec3,
// Color, Ref, std::string …) is initialized directly from one `Any`, so brace elision never kicks in.
// Limitations: C arrays as members count as their element count (use Vec2/3/4 instead — components
// are not allowed to have arrays, CONVENTIONS.md); the search stops at kMaxMembers.
#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

namespace akeir {

namespace detail {

struct AnyMember {
    template <class T> operator T() const;   // never defined — unevaluated context only
};

template <class T, class... Init>
constexpr bool constructibleWith() { return requires { T{std::declval<Init>()...}; }; }

inline constexpr std::size_t kMaxMembers = 128;   // CONVENTIONS.md: a component has at most 128 members

template <class T, class... Init>
constexpr std::size_t arityFrom() {
    if constexpr (sizeof...(Init) >= kMaxMembers) return kMaxMembers;
    else if constexpr (constructibleWith<T, Init..., AnyMember>()) return arityFrom<T, Init..., AnyMember>();
    else return sizeof...(Init);
}

} // namespace detail

/// Number of non-static data members of aggregate T (base classes count as one member each).
template <class T>
constexpr std::size_t aggregateArity() {
    static_assert(std::is_aggregate_v<T>, "aggregateArity<T>: T must be an aggregate");
    return detail::arityFrom<T>();
}

} // namespace akeir
