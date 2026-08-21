// akeir/serialization/Canonical.cpp — 설계 문서 §5.3, §9.2 (temp+rename), RFC 8785
#include "akeir/serialization/Canonical.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace akeir {

namespace {

void writeString(std::string& out, const std::string& s) {
    // nlohmann 의 escape 규칙과 같게 (dump 가 만드는 문자열을 그대로 쓴다)
    out += Json(s).dump();
}

bool writeNumber(std::string& out, const Json& j) {
    if (j.is_number_integer()) {
        if (j.is_number_unsigned()) out += std::to_string(j.get<std::uint64_t>());
        else out += std::to_string(j.get<std::int64_t>());
        return true;
    }
    double d = j.get<double>();
    if (std::isnan(d) || std::isinf(d)) return false;
    // nlohmann dump 는 shortest round-trip(double) 을 낸다. 정수값 double 은 "1.0" 으로 나오므로 §5.3 "정수에 .0 금지" 규칙에 맞춰
    // 정수값이면 정수로 쓴다. (float32 canonicalization 은 canonicalizeFloat 가 선행)
    if (d == std::floor(d) && std::fabs(d) < 1e15) { out += std::to_string(static_cast<long long>(d)); return true; }
    out += Json(d).dump();
    return true;
}

bool isScalar(const Json& j) { return !j.is_object() && !j.is_array(); }
bool allScalars(const Json& arr) { for (const auto& e : arr) if (!isScalar(e)) return false; return true; }

bool writeValue(std::string& out, const Json& j, int depth) {
    const std::string indent(static_cast<std::size_t>(depth) * 2, ' ');
    const std::string indentIn(static_cast<std::size_t>(depth + 1) * 2, ' ');
    if (j.is_null()) { out += "null"; return true; }
    if (j.is_boolean()) { out += j.get<bool>() ? "true" : "false"; return true; }
    if (j.is_number()) return writeNumber(out, j);
    if (j.is_string()) { writeString(out, j.get<std::string>()); return true; }
    if (j.is_array()) {
        if (j.empty()) { out += "[]"; return true; }
        if (allScalars(j)) {
            out += "[";
            for (std::size_t i = 0; i < j.size(); ++i) { if (i) out += ", "; if (!writeValue(out, j[i], depth)) return false; }
            out += "]";
            return true;
        }
        out += "[\n";
        for (std::size_t i = 0; i < j.size(); ++i) {
            out += indentIn;
            if (!writeValue(out, j[i], depth + 1)) return false;
            if (i + 1 < j.size()) out += ",";
            out += "\n";
        }
        out += indent + "]";
        return true;
    }
    // object
    if (j.empty()) { out += "{}"; return true; }
    out += "{\n";
    std::size_t i = 0;
    for (auto it = j.begin(); it != j.end(); ++it, ++i) {
        out += indentIn;
        writeString(out, it.key());
        out += ": ";
        if (!writeValue(out, it.value(), depth + 1)) return false;
        if (i + 1 < j.size()) out += ",";
        out += "\n";
    }
    out += indent + "}";
    return true;
}

// RFC 8785: 키를 UTF-16 code unit 순으로 정렬
std::u16string toUtf16(const std::string& s) {
    std::u16string out;
    for (std::size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp;
        std::size_t len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c >> 5) == 0x6) { cp = c & 0x1F; len = 2; }
        else if ((c >> 4) == 0xE) { cp = c & 0x0F; len = 3; }
        else { cp = c & 0x07; len = 4; }
        for (std::size_t k = 1; k < len && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        i += len;
        if (cp >= 0x10000) { cp -= 0x10000; out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10))); out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF))); }
        else out.push_back(static_cast<char16_t>(cp));
    }
    return out;
}

void jcsValue(std::string& out, const Json& j) {
    if (j.is_object()) {
        std::vector<std::pair<std::u16string, const Json::object_t::value_type*>> keys;
        // ordered_json 의 object_t 는 벡터형 ordered_map — 반복자로 수집
        std::vector<std::pair<std::string, const Json*>> items;
        for (auto it = j.begin(); it != j.end(); ++it) items.emplace_back(it.key(), &it.value());
        std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return toUtf16(a.first) < toUtf16(b.first); });
        out += "{";
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i) out += ",";
            out += Json(items[i].first).dump();
            out += ":";
            jcsValue(out, *items[i].second);
        }
        out += "}";
        return;
    }
    if (j.is_array()) {
        out += "[";
        for (std::size_t i = 0; i < j.size(); ++i) { if (i) out += ","; jcsValue(out, j[i]); }
        out += "]";
        return;
    }
    if (j.is_number_float()) {
        double d = j.get<double>();
        if (d == std::floor(d) && std::fabs(d) < 1e15) { out += std::to_string(static_cast<long long>(d)); return; } // ES6: 1.0 → "1"
        out += Json(d).dump();
        return;
    }
    out += j.dump();
}

} // namespace

std::optional<std::string> canonicalDump(const Json& doc) {
    std::string out;
    if (!writeValue(out, doc, 0)) return std::nullopt;
    out += "\n";
    return out;
}

std::string jcsDump(const Json& doc) {
    std::string out;
    jcsValue(out, doc);
    return out;
}

bool writeCanonicalFile(const std::string& path, const Json& doc, std::string* error) {
    auto text = canonicalDump(doc);
    if (!text) { if (error) *error = "document contains NaN/Inf (not representable in JSON, §5.3)"; return false; }
    namespace fs = std::filesystem;
    fs::path target(path);
    std::error_code ec;
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    fs::path tmp = target;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) { if (error) *error = "cannot open temp file " + tmp.string(); return false; }
        out.write(text->data(), static_cast<std::streamsize>(text->size()));
        out.flush();
        if (!out) { if (error) *error = "write failed " + tmp.string(); return false; }
    }
    fs::rename(tmp, target, ec);   // Windows: MoveFileEx(REPLACE_EXISTING). 원자성 미보장 → §9.2 journal 이 보완
    if (ec) { if (error) *error = "rename failed: " + ec.message(); fs::remove(tmp); return false; }
    return true;
}

std::optional<Json> parseJson(std::string_view text, std::string* error) {
    try {
        return Json::parse(text.begin(), text.end(), nullptr, true, false /* no comments (§5.3) */);
    } catch (const Json::parse_error& e) {
        if (error) *error = std::string(e.what()) + " (byte " + std::to_string(e.byte) + ")";
        return std::nullopt;
    }
}

std::optional<Json> readJsonFile(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { if (error) *error = "cannot open " + path; return std::nullopt; }
    std::stringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        text.erase(0, 3); // BOM 은 읽을 때 관용, 쓸 때는 없음
    return parseJson(text, error);
}

double canonicalizeFloat(float v) {
    char buf[64];
    auto [end, ec] = std::to_chars(buf, buf + sizeof buf, v); // shortest round-trip for float
    if (ec != std::errc{}) return static_cast<double>(v);
    double d = 0;
    std::from_chars(buf, end, d);
    return d;
}

Json canonicalizeFloats(const Json& j) {
    if (j.is_number_float()) return canonicalizeFloat(j.get<float>());
    if (j.is_array()) { Json a = Json::array(); for (const auto& e : j) a.push_back(canonicalizeFloats(e)); return a; }
    if (j.is_object()) { Json o = Json::object(); for (auto it = j.begin(); it != j.end(); ++it) o[it.key()] = canonicalizeFloats(it.value()); return o; }
    return j;
}

bool isCanonicalText(std::string_view fileText, const Json& parsed) {
    auto c = canonicalDump(parsed);
    return c && *c == fileText;
}

} // namespace akeir
