// pme/testing/Expr.h — Test assertion 표현식 (§23.1 "고정 비교 문법"). 언어가 아니라 비교기다: 루프·대입·함수 정의 없음, 종료·부작용 보장.
//
//   문법 (CEL 부분집합과 호환):
//     expr    := or ;  or := and ('||' and)* ;  and := rel ('&&' rel)*
//     rel     := add (('=='|'!='|'<'|'<='|'>'|'>='|'in') add)?
//     add     := mul (('+'|'-') mul)* ;  mul := unary (('*'|'/'|'%') unary)*
//     unary   := ('!'|'-') unary | postfix
//     postfix := primary ( '.' IDENT | '.' IDENT '(' args ')' | '[' expr ']' )*
//     primary := NUMBER | STRING | true | false | null | '[' args ']' | IDENT | IDENT '(' args ')' | '(' expr ')'
//   전역 함수: has(path) size(x) abs(x) dist(a, b) min(a, b) max(a, b)
//   리스트 매크로: <list>.all(e, pred) .exists(e, pred) .exists_one(e, pred) .size()
//
//   평가 대상은 frame snapshot JSON(§26.1). 바인딩:
//     <as 이름>  → 그 entity 의 components 객체        (player.Health.current)
//     world      → snapshot 전체                        (world.tick, world.entities — entity 배열 그대로, e.components.X)
//   존재하지 않는 멤버는 "undefined" 가 되고, has() 외의 연산에 닿으면 EvalError (false 가 아니라 오류 — 오타를 숨기지 않는다).
#pragma once

#include "pme/core/Json.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pme::expr {

struct ParseError { std::string message; std::size_t offset = 0; };
struct EvalError { std::string message; };

struct Node;
using NodePtr = std::shared_ptr<const Node>;

/// 파싱된 표현식. 재사용 가능 (매 tick 평가).
class Expr {
public:
    /// 실패 시 nullopt + error
    static std::optional<Expr> parse(std::string_view text, ParseError* error = nullptr);
    /// 바인딩 위에서 평가. 결과는 Json. 타입 오류/undefined 는 EvalError 를 던진다.
    Json eval(const std::map<std::string, Json>& bindings) const;
    /// 편의: bool 결과. bool 이 아니면 EvalError.
    bool evalBool(const std::map<std::string, Json>& bindings) const;
    /// 표현식이 참조하는 최상위 식별자 (`player`, `world`) — 보고서의 bindings 추출용
    std::vector<std::string> roots() const;
    /// 표현식 안의 모든 경로(`player.Health.current`)를 평가해 {path: value} 로 (§24 failures[].bindings)
    Json probeBindings(const std::map<std::string, Json>& bindings) const;
    const std::string& text() const { return text_; }

private:
    std::string text_;
    NodePtr root_;
};

} // namespace pme::expr
