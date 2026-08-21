// pme/reflection/Reflect.h — 등록 front-end (매크로 + builder). 설계 문서 §42.2.
//
//   PME_REFLECT_BEGIN(Health, "Hit points and death state")
//       PME_REQUIRES("Transform");
//       PME_LIFECYCLE("OnSpawn", "after EnemyAI", "OnDespawn");
//       PME_PROP(max, "Maximum hit points").min(1).def(100).unit("hp").ui(1, 1000, 1);
//       PME_PROP(current, "Current hit points").runtimeOnly().readOnly();
//   PME_REFLECT_END(Health)
//
//   enum 은 선언 뒤 전역 네임스페이스에서 이름을 등록한다 (값 = 선언 순서 index):
//       enum class ColliderShape { Box, Sphere, Capsule };
//       PME_REFLECT_ENUM(ColliderShape, "box", "sphere", "capsule")
//
//   component 는 aggregate struct 로 유지한다 (§42.2 / CONVENTIONS.md): 생성자·virtual·private 없음.
//   지원 leaf 타입: bool, int32/int64, float, double, std::string, reflect 된 enum, Vec2/3/4, Quat, Color, Ref.
#pragma once

#include "pme/core/Math.h"
#include "pme/core/Ref.h"
#include "pme/reflection/Registry.h"

#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace pme {

// ---------------------------------------------------------------- enum names

template <class E> struct EnumTraits { static constexpr bool kDefined = false; };

namespace detail {
std::vector<std::string> splitNames(const char* commaSeparated); // "\"box\", \"sphere\"" → {"box","sphere"}
}

#define PME_REFLECT_ENUM(EnumType, ...)                                                                 \
    template <> struct pme::EnumTraits<EnumType> {                                                      \
        static constexpr bool kDefined = true;                                                          \
        static const std::vector<std::string>& names() {                                                \
            static const std::vector<std::string> n = ::pme::detail::splitNames(#__VA_ARGS__);          \
            return n;                                                                                   \
        }                                                                                               \
    };

// ---------------------------------------------------------------- type traits

template <class T, class = void> struct PropTraits;                       // 미지원 타입은 컴파일 오류

template <> struct PropTraits<bool> {
    static constexpr PropType kType = PropType::Bool;
    static Json get(const bool& v) { return v; }
    static bool set(bool& v, const Json& j) { if (!j.is_boolean()) return false; v = j.get<bool>(); return true; }
};
template <class I> struct PropTraits<I, std::enable_if_t<std::is_integral_v<I> && !std::is_same_v<I, bool>>> {
    static constexpr PropType kType = PropType::Int;
    static Json get(const I& v) { return static_cast<std::int64_t>(v); }
    static bool set(I& v, const Json& j) { if (!j.is_number_integer()) return false; v = static_cast<I>(j.get<std::int64_t>()); return true; }
};
template <class F> struct PropTraits<F, std::enable_if_t<std::is_floating_point_v<F>>> {
    static constexpr PropType kType = PropType::Float;
    static Json get(const F& v) { return static_cast<double>(v); }
    static bool set(F& v, const Json& j) { if (!j.is_number()) return false; v = static_cast<F>(j.get<double>()); return true; }
};
template <> struct PropTraits<std::string> {
    static constexpr PropType kType = PropType::String;
    static Json get(const std::string& v) { return v; }
    static bool set(std::string& v, const Json& j) { if (!j.is_string()) return false; v = j.get<std::string>(); return true; }
};
template <class E> struct PropTraits<E, std::enable_if_t<std::is_enum_v<E>>> {
    static_assert(EnumTraits<E>::kDefined, "enum must be registered with PME_REFLECT_ENUM(EnumType, \"a\", \"b\", ...)");
    static constexpr PropType kType = PropType::Enum;
    static Json get(const E& v) {
        const auto& n = EnumTraits<E>::names();
        auto i = static_cast<std::size_t>(v);
        return i < n.size() ? Json(n[i]) : Json(nullptr);
    }
    static bool set(E& v, const Json& j) {
        if (!j.is_string()) return false;
        const auto& n = EnumTraits<E>::names();
        for (std::size_t i = 0; i < n.size(); ++i) if (n[i] == j.get<std::string>()) { v = static_cast<E>(i); return true; }
        return false;
    }
};
template <class V, PropType kT> struct VecTraits {
    static constexpr PropType kType = kT;
    static Json get(const V& v) { Json a = Json::array(); for (std::size_t i = 0; i < V::kSize; ++i) a.push_back(static_cast<double>(v[i])); return a; }
    static bool set(V& v, const Json& j) {
        if (!j.is_array() || j.size() != V::kSize) return false;
        for (std::size_t i = 0; i < V::kSize; ++i) if (!j[i].is_number()) return false;
        for (std::size_t i = 0; i < V::kSize; ++i) v[i] = j[i].get<float>();
        return true;
    }
};
template <> struct PropTraits<Vec2> : VecTraits<Vec2, PropType::Vec2> {};
template <> struct PropTraits<Vec3> : VecTraits<Vec3, PropType::Vec3> {};
template <> struct PropTraits<Vec4> : VecTraits<Vec4, PropType::Vec4> {};
template <> struct PropTraits<Quat> : VecTraits<Quat, PropType::Quat> {};
template <> struct PropTraits<Color> : VecTraits<Color, PropType::Color> {};
template <> struct PropTraits<Ref> {
    static constexpr PropType kType = PropType::Ref;
    static Json get(const Ref& v) { return v.value; }
    static bool set(Ref& v, const Json& j) { if (!j.is_string()) return false; if (!Ref::validate(j.get<std::string>()).empty()) return false; v.value = j.get<std::string>(); return true; }
};

// ---------------------------------------------------------------- builder

class PropBuilder {
public:
    explicit PropBuilder(PropertyMeta& m) : m_(m) {}
    PropBuilder& min(double v) { m_.minimum = v; return *this; }
    PropBuilder& max(double v) { m_.maximum = v; return *this; }
    PropBuilder& range(double lo, double hi) { m_.minimum = lo; m_.maximum = hi; return *this; }
    PropBuilder& warn(double lo, double hi) { m_.warnMin = lo; m_.warnMax = hi; return *this; }
    PropBuilder& ui(double lo, double hi, double step = 0) { m_.uiMin = lo; m_.uiMax = hi; if (step > 0) m_.step = step; return *this; }
    PropBuilder& multipleOf(double v) { m_.multipleOf = v; return *this; }
    PropBuilder& def(Json v) { m_.defaultValue = std::move(v); return *this; }
    PropBuilder& unit(std::string u) { m_.unit = std::move(u); return *this; }
    PropBuilder& category(std::string c) { m_.category = std::move(c); return *this; }
    PropBuilder& refType(std::string t) { m_.refType = std::move(t); m_.flags = m_.flags | PropFlags::Ref; return *this; }
    PropBuilder& runtimeOnly() { m_.flags = m_.flags | PropFlags::RuntimeOnly; return *this; }
    PropBuilder& readOnly() { m_.flags = m_.flags | PropFlags::ReadOnly; return *this; }
    PropBuilder& hidden() { m_.flags = m_.flags | PropFlags::Hidden; return *this; }
    PropBuilder& advanced() { m_.flags = m_.flags | PropFlags::Advanced; return *this; }
    PropBuilder& transient() { m_.flags = m_.flags | PropFlags::Transient; return *this; }
    PropBuilder& required() { m_.flags = m_.flags | PropFlags::Required; return *this; }
    PropBuilder& save() { m_.flags = m_.flags | PropFlags::Save; return *this; }
private:
    PropertyMeta& m_;
};

template <class T>
class ComponentBuilder {
public:
    ComponentBuilder(std::string name, std::string description) {
        static_assert(std::is_aggregate_v<T>, "components must be aggregate structs (§42.2 / CONVENTIONS.md)");
        meta_.name = std::move(name);
        meta_.description = std::move(description);
        meta_.size = sizeof(T);
        meta_.align = alignof(T);
        meta_.construct = [](void* p) { new (p) T{}; };
        meta_.destroy = [](void* p) { static_cast<T*>(p)->~T(); };
        meta_.copy = [](void* dst, const void* src) { *static_cast<T*>(dst) = *static_cast<const T*>(src); };
        meta_.move = [](void* dst, void* src) { *static_cast<T*>(dst) = std::move(*static_cast<T*>(src)); };
        meta_.triviallyCopyable = std::is_trivially_copyable_v<T>;
    }
    ComponentBuilder& requiresComponent(std::string componentName) { meta_.requiresComponents.push_back(std::move(componentName)); return *this; }
    ComponentBuilder& version(int v) { meta_.version = v; return *this; }
    ComponentBuilder& lifecycle(std::string init, std::string tick, std::string destroy) {
        meta_.lifecycle = Json{{"init", std::move(init)}, {"tick", std::move(tick)}, {"destroy", std::move(destroy)}};
        return *this;
    }

    template <class M>
    PropBuilder prop(M T::*member, std::string name, std::string description) {
        using Tr = PropTraits<M>;
        PropertyMeta m;
        m.name = std::move(name);
        m.description = std::move(description);
        m.type = Tr::kType;
        if constexpr (Tr::kType == PropType::Enum) m.enumOptions = EnumTraits<M>::names();
        // offset: aggregate 의 default 인스턴스에서 계산 (offsetof 는 비표준 레이아웃에서 경고)
        T probe{};
        m.offset = static_cast<std::size_t>(reinterpret_cast<const char*>(&(probe.*member)) - reinterpret_cast<const char*>(&probe));
        m.defaultValue = Tr::get(probe.*member);
        m.get = [member](const void* c) -> Json { return Tr::get(static_cast<const T*>(c)->*member); };
        m.set = [member](void* c, const Json& j) -> bool { return Tr::set(static_cast<T*>(c)->*member, j); };
        meta_.props.push_back(std::move(m));
        return PropBuilder(meta_.props.back());
    }

    bool finish() { return Registry::global().add(std::move(meta_), std::type_index(typeid(T))); }

private:
    ComponentMeta meta_;
};

} // namespace pme

// ---------------------------------------------------------------- macros

#define PME_REFLECT_BEGIN(Type, Description)                                                   \
    namespace {                                                                                \
    struct PmeReflect_##Type {                                                                 \
        PmeReflect_##Type() {                                                                  \
            using PmeSelf_ = Type;                                                             \
            ::pme::ComponentBuilder<Type> b_(#Type, Description);

#define PME_REQUIRES(Name) b_.requiresComponent(Name)
#define PME_VERSION(N) b_.version(N)
#define PME_LIFECYCLE(Init, Tick, Destroy) b_.lifecycle(Init, Tick, Destroy)
#define PME_PROP(Member, Description) b_.prop(&PmeSelf_::Member, #Member, Description)

#define PME_REFLECT_END(Type)                                                                  \
            b_.finish();                                                                       \
        }                                                                                      \
    };                                                                                         \
    static PmeReflect_##Type pmeReflectInstance_##Type;                                        \
    }

// 주의 (정적 라이브러리): 등록은 static initializer 로 일어난다. 링커는 참조되지 않는 .obj 를 버리므로
// component 만 담긴 정적 라이브러리는 $<LINK_LIBRARY:WHOLE_ARCHIVE,lib> 로 링크한다 (Engine/Reflection/README.md).
