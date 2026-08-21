// pme/core/Math.h — reflection 이 아는 고정 leaf 타입. 설계 문서 §5.3 (vec 는 고정 길이 배열), §14.1 (mutation path 는 /position/0), §42.2 (중첩 경로).
//
//   JSON 표현: Vec2 [x,y], Vec3 [x,y,z], Vec4/Quat/Color [x,y,z,w] — 객체 {x:,y:} 가 아니다.
//   성분 접근 경로: /position/0 (index). /position/x 는 없다.
#pragma once

#include <cstddef>

namespace pme {

struct Vec2 { float x = 0, y = 0; float& operator[](std::size_t i) { return i == 0 ? x : y; } float operator[](std::size_t i) const { return i == 0 ? x : y; } static constexpr std::size_t kSize = 2; bool operator==(const Vec2&) const = default; };
struct Vec3 { float x = 0, y = 0, z = 0; float& operator[](std::size_t i) { return i == 0 ? x : i == 1 ? y : z; } float operator[](std::size_t i) const { return i == 0 ? x : i == 1 ? y : z; } static constexpr std::size_t kSize = 3; bool operator==(const Vec3&) const = default; };
struct Vec4 { float x = 0, y = 0, z = 0, w = 0; float& operator[](std::size_t i) { return i == 0 ? x : i == 1 ? y : i == 2 ? z : w; } float operator[](std::size_t i) const { return i == 0 ? x : i == 1 ? y : i == 2 ? z : w; } static constexpr std::size_t kSize = 4; bool operator==(const Vec4&) const = default; };
struct Quat { float x = 0, y = 0, z = 0, w = 1; float& operator[](std::size_t i) { return i == 0 ? x : i == 1 ? y : i == 2 ? z : w; } float operator[](std::size_t i) const { return i == 0 ? x : i == 1 ? y : i == 2 ? z : w; } static constexpr std::size_t kSize = 4; bool operator==(const Quat&) const = default; };
struct Color { float r = 1, g = 1, b = 1, a = 1; float& operator[](std::size_t i) { return i == 0 ? r : i == 1 ? g : i == 2 ? b : a; } float operator[](std::size_t i) const { return i == 0 ? r : i == 1 ? g : i == 2 ? b : a; } static constexpr std::size_t kSize = 4; bool operator==(const Color&) const = default; };

} // namespace pme
