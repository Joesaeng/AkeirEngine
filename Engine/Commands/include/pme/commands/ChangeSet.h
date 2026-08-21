// pme/commands/ChangeSet.h — 모든 mutation 의 효과 기록. 설계 문서 §78 (RFC 6902 superset, self-inverting), §78.1 (Command → ops 매핑), §10.1 (inverse), §51 (semantic diff).
//
//   ChangeOp  = { op: add|remove|replace|move|test|file.add|file.remove|file.replace,
//                 doc: 프로젝트 상대 경로, path: RFC 6901 (""=문서 전체), [from], [value], [before], [blob], [beforeBlob] }
//   규칙 (§78):
//     1. path 는 JSON Pointer. dotted path 금지.                 2. remove/replace/move/file.* 는 반드시 before 를 싣는다 (self-inverting)
//     3. copy 금지 (비가역)                                      4. 바이너리는 file.* op + content-addressed blob
//     5. replace 의 새 값은 `value` (RFC 6902 와 같다) → doc/before 를 떼면 그대로 nlohmann::json::patch() 입력
//     6. intent 는 audit 용, redo 는 effect(ops) 재적용            7. base = touched 문서의 commit 시점 hash (optimistic concurrency)
//     8. command 가 ChangeBuilder 로 직접 emit — json::diff 로 사후 생성하지 않는다
#pragma once

#include "pme/core/Diagnostic.h"
#include "pme/core/Json.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pme {

struct ChangeOp {
    std::string op;
    std::string doc;
    std::string path;
    std::string from;            // move 전용
    Json value;                  // add / replace / test 의 값
    Json before;                 // remove / replace 의 이전 값 (move 는 from 의 값)
    std::string blob, beforeBlob; // file.* (sha256:…)

    Json toJson() const;
    static std::optional<ChangeOp> fromJson(const Json& j);
    /// RFC 6902 형식 (doc/before 제거). nlohmann patch() 에 바로 넣을 수 있다.
    Json toRfc6902() const;
    bool isFileOp() const { return op.rfind("file.", 0) == 0; }
};

struct ChangeSet {
    int changeSetVersion = 1;
    std::string id;              // cs_…
    std::string tx;              // tx_… 또는 빈 문자열
    std::string actor;           // ai:claude#42 | human:editor | system:migrate | cli
    std::string createdAt;       // ISO-8601
    Json intent = Json::object();             // {op, args, resolved} 또는 배열(tx compose)
    Json base = Json::object();               // {doc: "sha256:…"}
    std::vector<ChangeOp> ops;
    std::vector<std::string> touched;         // 문서 경로 (정렬, 중복 없음)
    bool lossy = false;
    std::vector<Diagnostic> diagnostics;

    Json toJson() const;
    static std::optional<ChangeSet> fromJson(const Json& j);
    /// §10.1 inverse(ops): 역순 + op 별 역연산. id/intent 는 새로 받는다.
    ChangeSet inverse() const;
    /// ops 에서 touched / summary 갱신
    void finalize();
    Json summary() const;
    /// §9.2 compose: 여러 ChangeSet 을 순서대로 하나로 (tx commit)
    static ChangeSet compose(const std::vector<ChangeSet>& parts, const std::string& txId);
    bool empty() const { return ops.empty(); }
};

/// 문서 맵(path → Json)에 ops 를 순서대로 적용한다. 한 op 라도 실패하면 false + diagnostics (문서 맵은 부분 적용된 상태일 수 있으니 호출자가 fork 에 적용한다, §50).
bool applyOps(std::map<std::string, Json>& docs, const std::vector<ChangeOp>& ops, std::vector<Diagnostic>* diagnostics = nullptr);

/// RFC 6901 escape: "~"→"~0", "/"→"~1"
std::string escapeToken(std::string_view token);
std::string unescapeToken(std::string_view token);

} // namespace pme
