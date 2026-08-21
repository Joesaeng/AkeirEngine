// akeir/reflection/Registry.cpp — 설계 문서 §42.2, §14.1
#include "akeir/reflection/Registry.h"

#include <cctype>
#include <charconv>

namespace akeir {

namespace detail {
std::vector<std::string> splitNames(const char* commaSeparated) {
    // #__VA_ARGS__ 는 '"box", "sphere", "capsule"' 형태. 따옴표와 공백을 벗긴다.
    std::vector<std::string> out;
    std::string cur;
    bool inQuote = false;
    for (const char* p = commaSeparated; *p; ++p) {
        char c = *p;
        if (c == '"') { inQuote = !inQuote; continue; }
        if (!inQuote) {
            if (c == ',') { out.push_back(cur); cur.clear(); }
            else if (!std::isspace(static_cast<unsigned char>(c))) cur.push_back(c); // 따옴표 없는 식별자도 허용
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty() || !out.empty()) out.push_back(cur);
    return out;
}
} // namespace detail

Registry& Registry::global() {
    static Registry r;
    return r;
}

bool Registry::add(ComponentMeta meta, std::type_index type) {
    if (byName_.count(meta.name)) return false;
    auto ptr = std::make_unique<ComponentMeta>(std::move(meta));
    ComponentMeta* raw = ptr.get();
    byName_[raw->name] = std::move(ptr);
    byType_[type] = raw;
    return true;
}

const ComponentMeta* Registry::find(std::string_view name) const {
    auto it = byName_.find(name);
    return it == byName_.end() ? nullptr : it->second.get();
}

const ComponentMeta* Registry::find(std::type_index type) const {
    auto it = byType_.find(type);
    return it == byType_.end() ? nullptr : it->second;
}

std::vector<const ComponentMeta*> Registry::all() const {
    std::vector<const ComponentMeta*> out;
    out.reserve(byName_.size());
    for (const auto& [k, v] : byName_) out.push_back(v.get()); // std::map → 이름순 (결정적)
    return out;
}

void Registry::clear() { byType_.clear(); byName_.clear(); }

std::optional<std::pair<std::string, std::optional<std::size_t>>> splitPropertyPointer(std::string_view pointer) {
    if (pointer.empty() || pointer[0] != '/') return std::nullopt;
    pointer.remove_prefix(1);
    auto slash = pointer.find('/');
    std::string prop(pointer.substr(0, slash));
    if (prop.empty()) return std::nullopt;
    if (slash == std::string_view::npos) return std::make_pair(prop, std::optional<std::size_t>{});
    std::string_view rest = pointer.substr(slash + 1);
    if (rest.empty() || rest.find('/') != std::string_view::npos) return std::nullopt;
    std::size_t idx = 0;
    auto [ptr, ec] = std::from_chars(rest.data(), rest.data() + rest.size(), idx);
    if (ec != std::errc{} || ptr != rest.data() + rest.size()) return std::nullopt;
    return std::make_pair(prop, std::optional<std::size_t>{idx});
}

std::optional<Json> getByPointer(const ComponentMeta& meta, const void* component, std::string_view pointer) {
    auto split = splitPropertyPointer(pointer);
    if (!split) return std::nullopt;
    const PropertyMeta* p = meta.find(split->first);
    if (!p) return std::nullopt;
    Json v = p->get(component);
    if (!split->second) return v;
    if (!v.is_array() || *split->second >= v.size()) return std::nullopt;
    return v[*split->second];
}

bool setByPointer(const ComponentMeta& meta, void* component, std::string_view pointer, const Json& value, bool allowReadOnly) {
    auto split = splitPropertyPointer(pointer);
    if (!split) return false;
    const PropertyMeta* p = meta.find(split->first);
    if (!p) return false;
    if (!allowReadOnly && has(p->flags, PropFlags::ReadOnly)) return false;
    if (!split->second) return p->set(component, value);
    // leaf 배열 성분: 읽어서 한 성분만 바꾸고 다시 쓴다
    Json cur = p->get(component);
    if (!cur.is_array() || *split->second >= cur.size() || !value.is_number()) return false;
    cur[*split->second] = value;
    return p->set(component, cur);
}

Json allSchemas() {
    Json j = Json::object();
    for (const auto* m : Registry::global().all()) j[m->name] = m->toSchema();
    return j;
}

} // namespace akeir
