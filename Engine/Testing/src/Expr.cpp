// Expr.cpp — §23.1 고정 비교 문법: 토크나이저 → 재귀 하강 파서 → AST 평가
#include "pme/testing/Expr.h"

#include <cmath>
#include <cctype>
#include <functional>
#include <set>

namespace pme::expr {

// ---------------------------------------------------------------- AST

enum class Kind { Literal, Ident, Member, Index, Call, Macro, Unary, Binary, List };

struct Node {
    Kind kind;
    Json literal;                       // Literal
    std::string name;                   // Ident / Member field / Call·Macro name / Unary·Binary op
    NodePtr a, b;                       // Unary: a. Binary: a,b. Member/Index: a (+ b for Index). Macro: a = list, b = predicate
    std::vector<NodePtr> args;          // Call / List
    std::string var;                    // Macro 의 바인딩 변수
};

namespace {

// ---------------------------------------------------------------- tokenizer

enum class Tok { End, Number, String, Ident, Op };
struct Token { Tok type; std::string text; double num = 0; std::size_t pos = 0; };

class Lexer {
public:
    explicit Lexer(std::string_view s) : s_(s) {}
    std::vector<Token> run(ParseError* err) {
        std::vector<Token> out;
        while (true) {
            skipWs();
            if (i_ >= s_.size()) { out.push_back({Tok::End, "", 0, i_}); return out; }
            char c = s_[i_];
            std::size_t start = i_;
            if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i_ + 1 < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_ + 1])))) {
                std::size_t j = i_;
                while (j < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[j])) || s_[j] == '.' || s_[j] == '+' || s_[j] == '-')) {
                    // 지수 뒤 부호만 허용
                    if ((s_[j] == '+' || s_[j] == '-') && !(j > 0 && (s_[j - 1] == 'e' || s_[j - 1] == 'E'))) break;
                    ++j;
                }
                std::string t(s_.substr(i_, j - i_));
                char* end = nullptr;
                double v = std::strtod(t.c_str(), &end);
                if (!end || *end != '\0') { if (err) *err = {"bad number '" + t + "'", start}; return {}; }
                out.push_back({Tok::Number, t, v, start});
                i_ = j;
                continue;
            }
            if (c == '"' || c == '\'') {
                char q = c;
                std::string v;
                ++i_;
                bool closed = false;
                while (i_ < s_.size()) {
                    char d = s_[i_++];
                    if (d == '\\' && i_ < s_.size()) {
                        char e = s_[i_++];
                        switch (e) { case 'n': v += '\n'; break; case 't': v += '\t'; break; case '"': v += '"'; break; case '\'': v += '\''; break; case '\\': v += '\\'; break; default: v += e; }
                        continue;
                    }
                    if (d == q) { closed = true; break; }
                    v += d;
                }
                if (!closed) { if (err) *err = {"unterminated string", start}; return {}; }
                out.push_back({Tok::String, v, 0, start});
                continue;
            }
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$') {
                std::size_t j = i_;
                while (j < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[j])) || s_[j] == '_' || s_[j] == '$')) ++j;
                out.push_back({Tok::Ident, std::string(s_.substr(i_, j - i_)), 0, start});
                i_ = j;
                continue;
            }
            static const char* ops2[] = {"==", "!=", "<=", ">=", "&&", "||", nullptr};
            bool matched = false;
            for (const char** p = ops2; *p; ++p) {
                if (s_.substr(i_, 2) == *p) { out.push_back({Tok::Op, *p, 0, start}); i_ += 2; matched = true; break; }
            }
            if (matched) continue;
            if (std::string_view("+-*/%<>!().,[]").find(c) != std::string_view::npos) { out.push_back({Tok::Op, std::string(1, c), 0, start}); ++i_; continue; }
            if (err) *err = {std::string("unexpected character '") + c + "'", start};
            return {};
        }
    }
private:
    std::string_view s_;
    std::size_t i_ = 0;
    void skipWs() { while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_]))) ++i_; }
};

// ---------------------------------------------------------------- parser

class Parser {
public:
    Parser(std::vector<Token> toks, ParseError* err) : t_(std::move(toks)), err_(err) {}
    NodePtr parseAll() {
        NodePtr n = parseOr();
        if (!n) return nullptr;
        if (peek().type != Tok::End) { fail("unexpected token '" + peek().text + "'"); return nullptr; }
        return n;
    }
private:
    std::vector<Token> t_;
    std::size_t i_ = 0;
    ParseError* err_;
    bool failed_ = false;

    const Token& peek() const { return t_[i_]; }
    Token next() { return t_[i_ < t_.size() - 1 ? i_++ : i_]; }
    bool isOp(const char* s) const { return peek().type == Tok::Op && peek().text == s; }
    bool isIdent(const char* s) const { return peek().type == Tok::Ident && peek().text == s; }
    bool accept(const char* s) { if (isOp(s)) { next(); return true; } return false; }
    void fail(const std::string& m) { if (!failed_ && err_) *err_ = {m, peek().pos}; failed_ = true; }
    std::shared_ptr<Node> make(Kind k) { auto n = std::make_shared<Node>(); n->kind = k; return n; }

    NodePtr parseOr() {
        NodePtr l = parseAnd();
        while (l && isOp("||")) { next(); NodePtr r = parseAnd(); if (!r) return nullptr; auto n = make(Kind::Binary); n->name = "||"; n->a = l; n->b = r; l = n; }
        return l;
    }
    NodePtr parseAnd() {
        NodePtr l = parseRel();
        while (l && isOp("&&")) { next(); NodePtr r = parseRel(); if (!r) return nullptr; auto n = make(Kind::Binary); n->name = "&&"; n->a = l; n->b = r; l = n; }
        return l;
    }
    NodePtr parseRel() {
        NodePtr l = parseAdd();
        if (!l) return nullptr;
        static const char* rels[] = {"==", "!=", "<=", ">=", "<", ">", nullptr};
        for (const char** p = rels; *p; ++p) if (isOp(*p)) { std::string op = next().text; NodePtr r = parseAdd(); if (!r) return nullptr; auto n = make(Kind::Binary); n->name = op; n->a = l; n->b = r; return n; }
        if (isIdent("in")) { next(); NodePtr r = parseAdd(); if (!r) return nullptr; auto n = make(Kind::Binary); n->name = "in"; n->a = l; n->b = r; return n; }
        return l;
    }
    NodePtr parseAdd() {
        NodePtr l = parseMul();
        while (l && (isOp("+") || isOp("-"))) { std::string op = next().text; NodePtr r = parseMul(); if (!r) return nullptr; auto n = make(Kind::Binary); n->name = op; n->a = l; n->b = r; l = n; }
        return l;
    }
    NodePtr parseMul() {
        NodePtr l = parseUnary();
        while (l && (isOp("*") || isOp("/") || isOp("%"))) { std::string op = next().text; NodePtr r = parseUnary(); if (!r) return nullptr; auto n = make(Kind::Binary); n->name = op; n->a = l; n->b = r; l = n; }
        return l;
    }
    NodePtr parseUnary() {
        if (isOp("!") || isOp("-")) { std::string op = next().text; NodePtr a = parseUnary(); if (!a) return nullptr; auto n = make(Kind::Unary); n->name = op; n->a = a; return n; }
        return parsePostfix();
    }
    bool parseArgs(std::vector<NodePtr>& out, const char* close) {
        if (accept(close)) return true;
        while (true) {
            NodePtr a = parseOr();
            if (!a) return false;
            out.push_back(a);
            if (accept(",")) continue;
            if (accept(close)) return true;
            fail(std::string("expected ',' or '") + close + "'");
            return false;
        }
    }
    NodePtr parsePostfix() {
        NodePtr n = parsePrimary();
        while (n) {
            if (accept(".")) {
                if (peek().type != Tok::Ident) { fail("expected identifier after '.'"); return nullptr; }
                std::string field = next().text;
                if (accept("(")) {
                    if (field == "all" || field == "exists" || field == "exists_one") {
                        if (peek().type != Tok::Ident) { fail(field + "(var, predicate) expects an identifier"); return nullptr; }
                        auto m = make(Kind::Macro); m->name = field; m->a = n; m->var = next().text;
                        if (!accept(",")) { fail("expected ',' after macro variable"); return nullptr; }
                        m->b = parseOr(); if (!m->b) return nullptr;
                        if (!accept(")")) { fail("expected ')'"); return nullptr; }
                        n = m;
                    } else {
                        auto c = make(Kind::Call); c->name = field; c->args.push_back(n);   // method call: x.size() == size(x)
                        if (!parseArgs(c->args, ")")) return nullptr;
                        n = c;
                    }
                } else { auto m = make(Kind::Member); m->name = field; m->a = n; n = m; }
                continue;
            }
            if (accept("[")) {
                NodePtr idx = parseOr(); if (!idx) return nullptr;
                if (!accept("]")) { fail("expected ']'"); return nullptr; }
                auto m = make(Kind::Index); m->a = n; m->b = idx; n = m;
                continue;
            }
            break;
        }
        return n;
    }
    NodePtr parsePrimary() {
        const Token& t = peek();
        if (t.type == Tok::Number) { auto n = make(Kind::Literal); double v = next().num; if (v == std::floor(v) && std::fabs(v) < 9e15) n->literal = static_cast<std::int64_t>(v); else n->literal = v; return n; }
        if (t.type == Tok::String) { auto n = make(Kind::Literal); n->literal = next().text; return n; }
        if (t.type == Tok::Ident) {
            if (t.text == "true" || t.text == "false") { auto n = make(Kind::Literal); n->literal = next().text == "true"; return n; }
            if (t.text == "null") { next(); auto n = make(Kind::Literal); n->literal = nullptr; return n; }
            std::string name = next().text;
            if (accept("(")) { auto c = make(Kind::Call); c->name = name; if (!parseArgs(c->args, ")")) return nullptr; return c; }
            auto id = make(Kind::Ident); id->name = name; return id;
        }
        if (accept("[")) { auto l = make(Kind::List); if (!parseArgs(l->args, "]")) return nullptr; return l; }
        if (accept("(")) { NodePtr e = parseOr(); if (!e) return nullptr; if (!accept(")")) { fail("expected ')'"); return nullptr; } return e; }
        fail(t.type == Tok::End ? "unexpected end of expression" : "unexpected token '" + t.text + "'");
        return nullptr;
    }
};

// ---------------------------------------------------------------- evaluator

struct Value {
    Json j;
    bool defined = true;
    std::string path;   // 진단용 (undefined 일 때 어디서)
};

std::string describe(const Node& n) {
    switch (n.kind) {
        case Kind::Ident: return n.name;
        case Kind::Member: return describe(*n.a) + "." + n.name;
        case Kind::Index: return describe(*n.a) + "[…]";
        case Kind::Literal: return n.literal.dump();
        case Kind::Call: return n.name + "(…)";
        case Kind::Macro: return describe(*n.a) + "." + n.name + "(" + n.var + ", …)";
        case Kind::Unary: return n.name + describe(*n.a);
        case Kind::Binary: return "(" + describe(*n.a) + " " + n.name + " " + describe(*n.b) + ")";
        case Kind::List: return "[…]";
    }
    return "?";
}

double num(const Json& j, const std::string& where) {
    if (j.is_number()) return j.get<double>();
    if (j.is_boolean()) return j.get<bool>() ? 1.0 : 0.0;
    throw EvalError{where + ": expected a number, got " + std::string(j.type_name())};
}

bool jsonEq(const Json& a, const Json& b) {
    if (a.is_number() && b.is_number()) return a.get<double>() == b.get<double>();
    if (a.is_array() && b.is_array()) { if (a.size() != b.size()) return false; for (std::size_t i = 0; i < a.size(); ++i) if (!jsonEq(a[i], b[i])) return false; return true; }
    if (a.is_object() && b.is_object()) { if (a.size() != b.size()) return false; for (const auto& [k, v] : a.items()) { if (!b.contains(k) || !jsonEq(v, b[k])) return false; } return true; }
    return a == b;
}

Value require(Value v) {
    if (!v.defined) throw EvalError{"'" + v.path + "' is undefined (no such field/binding)"};
    return v;
}

class Evaluator {
public:
    explicit Evaluator(const std::map<std::string, Json>& b) : bindings_(b) {}

    Value eval(const Node& n) {
        switch (n.kind) {
            case Kind::Literal: return {n.literal, true, ""};
            case Kind::Ident: {
                for (auto it = locals_.rbegin(); it != locals_.rend(); ++it) if (it->first == n.name) return {it->second, true, n.name};
                auto it = bindings_.find(n.name);
                if (it == bindings_.end()) return {Json(), false, n.name};
                return {it->second, true, n.name};
            }
            case Kind::Member: {
                Value base = eval(*n.a);
                std::string path = base.path + "." + n.name;
                if (!base.defined) return {Json(), false, path};
                if (!base.j.is_object() || !base.j.contains(n.name)) return {Json(), false, path};
                return {base.j[n.name], true, path};
            }
            case Kind::Index: {
                Value base = require(eval(*n.a));
                Value idx = require(eval(*n.b));
                if (base.j.is_array()) {
                    double d = num(idx.j, "index");
                    auto i = static_cast<std::int64_t>(d);
                    if (i < 0 || static_cast<std::size_t>(i) >= base.j.size()) return {Json(), false, base.path + "[" + std::to_string(i) + "]"};
                    return {base.j[static_cast<std::size_t>(i)], true, base.path + "[" + std::to_string(i) + "]"};
                }
                if (base.j.is_object() && idx.j.is_string()) {
                    std::string k = idx.j.get<std::string>();
                    if (!base.j.contains(k)) return {Json(), false, base.path + "[\"" + k + "\"]"};
                    return {base.j[k], true, base.path + "[\"" + k + "\"]"};
                }
                throw EvalError{"cannot index " + std::string(base.j.type_name()) + " with " + std::string(idx.j.type_name())};
            }
            case Kind::List: {
                Json arr = Json::array();
                for (const auto& a : n.args) arr.push_back(require(eval(*a)).j);
                return {arr, true, ""};
            }
            case Kind::Unary: {
                Value v = require(eval(*n.a));
                if (n.name == "!") { if (!v.j.is_boolean()) throw EvalError{"'!' expects a boolean, got " + std::string(v.j.type_name())}; return {!v.j.get<bool>(), true, ""}; }
                return {-num(v.j, "unary -"), true, ""};
            }
            case Kind::Binary: return binary(n);
            case Kind::Call: return call(n);
            case Kind::Macro: return macro(n);
        }
        throw EvalError{"bad node"};
    }

private:
    const std::map<std::string, Json>& bindings_;
    std::vector<std::pair<std::string, Json>> locals_;

    Value binary(const Node& n) {
        const std::string& op = n.name;
        if (op == "&&" || op == "||") {
            Value l = require(eval(*n.a));
            if (!l.j.is_boolean()) throw EvalError{"'" + op + "' expects booleans, got " + std::string(l.j.type_name()) + " (" + describe(*n.a) + ")"};
            bool lv = l.j.get<bool>();
            if (op == "&&" && !lv) return {false, true, ""};
            if (op == "||" && lv) return {true, true, ""};
            Value r = require(eval(*n.b));
            if (!r.j.is_boolean()) throw EvalError{"'" + op + "' expects booleans, got " + std::string(r.j.type_name()) + " (" + describe(*n.b) + ")"};
            return {r.j.get<bool>(), true, ""};
        }
        Value l = require(eval(*n.a));
        Value r = require(eval(*n.b));
        if (op == "==") return {jsonEq(l.j, r.j), true, ""};
        if (op == "!=") return {!jsonEq(l.j, r.j), true, ""};
        if (op == "in") {
            if (r.j.is_array()) { for (const auto& x : r.j) if (jsonEq(x, l.j)) return {true, true, ""}; return {false, true, ""}; }
            if (r.j.is_object() && l.j.is_string()) return {r.j.contains(l.j.get<std::string>()), true, ""};
            if (r.j.is_string() && l.j.is_string()) return {r.j.get<std::string>().find(l.j.get<std::string>()) != std::string::npos, true, ""};
            throw EvalError{"'in' expects a list, object or string on the right"};
        }
        if (op == "+" && l.j.is_string() && r.j.is_string()) return {l.j.get<std::string>() + r.j.get<std::string>(), true, ""};
        if (op == "<" || op == "<=" || op == ">" || op == ">=") {
            if (l.j.is_string() && r.j.is_string()) {
                int c = l.j.get<std::string>().compare(r.j.get<std::string>());
                bool v = op == "<" ? c < 0 : op == "<=" ? c <= 0 : op == ">" ? c > 0 : c >= 0;
                return {v, true, ""};
            }
        }
        double a = num(l.j, describe(*n.a)), b = num(r.j, describe(*n.b));
        if (op == "<") return {a < b, true, ""};
        if (op == "<=") return {a <= b, true, ""};
        if (op == ">") return {a > b, true, ""};
        if (op == ">=") return {a >= b, true, ""};
        double res = 0;
        if (op == "+") res = a + b;
        else if (op == "-") res = a - b;
        else if (op == "*") res = a * b;
        else if (op == "/") { if (b == 0) throw EvalError{"division by zero"}; res = a / b; }
        else if (op == "%") { if (b == 0) throw EvalError{"modulo by zero"}; res = std::fmod(a, b); }
        else throw EvalError{"unknown operator " + op};
        if (l.j.is_number_integer() && r.j.is_number_integer() && op != "/" && res == std::floor(res)) return {static_cast<std::int64_t>(res), true, ""};
        return {res, true, ""};
    }

    Value call(const Node& n) {
        const std::string& f = n.name;
        auto arity = [&](std::size_t k) { if (n.args.size() != k) throw EvalError{f + "() expects " + std::to_string(k) + " argument(s)"}; };
        if (f == "has") { arity(1); Value v = eval(*n.args[0]); return {v.defined && !v.j.is_null(), true, ""}; }
        if (f == "size") {
            arity(1); Value v = require(eval(*n.args[0]));
            if (v.j.is_array() || v.j.is_object()) return {static_cast<std::int64_t>(v.j.size()), true, ""};
            if (v.j.is_string()) return {static_cast<std::int64_t>(v.j.get<std::string>().size()), true, ""};
            throw EvalError{"size() expects a list, object or string"};
        }
        if (f == "abs") { arity(1); return {std::fabs(num(require(eval(*n.args[0])).j, "abs")), true, ""}; }
        if (f == "min" || f == "max") {
            arity(2); double a = num(require(eval(*n.args[0])).j, f), b = num(require(eval(*n.args[1])).j, f);
            return {f == "min" ? std::min(a, b) : std::max(a, b), true, ""};
        }
        if (f == "dist") {
            arity(2); Value a = require(eval(*n.args[0])), b = require(eval(*n.args[1]));
            if (!a.j.is_array() || !b.j.is_array()) throw EvalError{"dist() expects two vectors (arrays)"};
            std::size_t k = std::min(a.j.size(), b.j.size());
            double s = 0;
            for (std::size_t i = 0; i < k; ++i) { double d = num(a.j[i], "dist") - num(b.j[i], "dist"); s += d * d; }
            return {std::sqrt(s), true, ""};
        }
        throw EvalError{"unknown function " + f + "() (allowed: has, size, abs, min, max, dist)"};
    }

    Value macro(const Node& n) {
        Value list = require(eval(*n.a));
        if (!list.j.is_array()) throw EvalError{n.name + "() expects a list, got " + std::string(list.j.type_name()) + " (" + describe(*n.a) + ")"};
        std::size_t hits = 0;
        for (const auto& item : list.j) {
            locals_.emplace_back(n.var, item);
            Value p = require(eval(*n.b));
            locals_.pop_back();
            if (!p.j.is_boolean()) throw EvalError{n.name + "() predicate must be boolean"};
            bool ok = p.j.get<bool>();
            if (n.name == "all" && !ok) return {false, true, ""};
            if (n.name == "exists" && ok) return {true, true, ""};
            if (ok) ++hits;
        }
        if (n.name == "all") return {true, true, ""};
        if (n.name == "exists") return {false, true, ""};
        return {hits == 1, true, ""};
    }
};

void collectPaths(const Node& n, std::vector<const Node*>& out) {
    // "가장 긴 멤버 경로" 만 수집 (player.Health.current 전체, 그 부분 경로는 제외)
    if (n.kind == Kind::Member || n.kind == Kind::Ident) { out.push_back(&n); return; }
    if (n.a) collectPaths(*n.a, out);
    if (n.b) collectPaths(*n.b, out);
    for (const auto& a : n.args) collectPaths(*a, out);
}

void collectRoots(const Node& n, std::set<std::string>& out) {
    if (n.kind == Kind::Ident) { out.insert(n.name); return; }
    if (n.a) collectRoots(*n.a, out);
    if (n.b) collectRoots(*n.b, out);
    for (const auto& a : n.args) collectRoots(*a, out);
}

} // namespace

// ---------------------------------------------------------------- Expr

std::optional<Expr> Expr::parse(std::string_view text, ParseError* error) {
    ParseError err;
    Lexer lx(text);
    auto toks = lx.run(&err);
    if (toks.empty()) { if (error) *error = err; return std::nullopt; }
    Parser p(std::move(toks), &err);
    NodePtr root = p.parseAll();
    if (!root) { if (error) *error = err; return std::nullopt; }
    Expr e;
    e.text_ = std::string(text);
    e.root_ = root;
    return e;
}

Json Expr::eval(const std::map<std::string, Json>& bindings) const {
    Evaluator ev(bindings);
    return require(ev.eval(*root_)).j;
}

bool Expr::evalBool(const std::map<std::string, Json>& bindings) const {
    Json v = eval(bindings);
    if (!v.is_boolean()) throw EvalError{"expression did not produce a boolean: " + v.dump()};
    return v.get<bool>();
}

std::vector<std::string> Expr::roots() const {
    std::set<std::string> s;
    collectRoots(*root_, s);
    return {s.begin(), s.end()};
}

Json Expr::probeBindings(const std::map<std::string, Json>& bindings) const {
    std::vector<const Node*> paths;
    collectPaths(*root_, paths);
    Json out = Json::object();
    Evaluator ev(bindings);
    for (const Node* n : paths) {
        std::string key = describe(*n);
        if (key == "world") continue;   // 전체 snapshot 은 싣지 않는다
        try {
            Value v = ev.eval(*n);
            if (!v.defined) out[key] = "<undefined>";
            else if (v.j.is_object() || (v.j.is_array() && v.j.size() > 16)) out[key] = std::string("<") + v.j.type_name() + " size " + std::to_string(v.j.size()) + ">";
            else out[key] = v.j;
        } catch (const EvalError& e) { out[key] = "<error: " + e.message + ">"; }
    }
    return out;
}

} // namespace pme::expr
