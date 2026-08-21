// pme/commands/History.h — ChangeSet 영속 저장소: journal(write-ahead), history(undo/redo 스택), idempotency 캐시.
// 설계 문서 §9.2 (commit 절차: journal → 파일 temp+rename → journal 삭제), §10 (history.jsonl, cursor, actor 태깅), §49 (idempotencyKey).
//
//   <project>/Cache/journal/<cs_id>.json     commit 중인 ChangeSet. 정상 commit 뒤 삭제. 남아 있으면 crash 복구 대상(§9.2 recover).
//   <project>/Cache/history/history.jsonl    commit 된 ChangeSet 한 줄씩 (append-only; redo 꼬리가 잘릴 때만 다시 쓴다)
//   <project>/Cache/history/cursor.json      { "cursor": N }  — entries[0..N) 이 "적용된" 상태. undo 는 N-1 을 뒤집고 N-- , redo 는 N 을 다시 적용하고 N++
//   <project>/Cache/history/idempotency.json { key: envelopeJson }   — apply --idempotency-key 재시도 시 같은 응답
//
//   Cache/ 는 VCS 에 넣지 않는다 (§5). history 는 프로젝트당 하나의 선형 스택이다 (§10: 브랜치 없음, 충돌은 conflict 로 보고).
#pragma once

#include "pme/commands/ChangeSet.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace pme {

class History {
public:
    explicit History(std::string projectRoot);

    /// 디스크에서 history.jsonl + cursor.json 을 읽는다. 파일이 없으면 빈 상태로 true.
    bool load(std::string* error = nullptr);
    const std::vector<ChangeSet>& entries() const { return entries_; }
    std::size_t cursor() const { return cursor_; }
    bool canUndo() const { return cursor_ > 0; }
    bool canRedo() const { return cursor_ < entries_.size(); }
    const ChangeSet* undoTarget() const { return canUndo() ? &entries_[cursor_ - 1] : nullptr; }
    const ChangeSet* redoTarget() const { return canRedo() ? &entries_[cursor_] : nullptr; }

    /// 새 commit: cursor 뒤의 redo 꼬리를 버리고 append, cursor = size. 디스크 반영.
    bool push(const ChangeSet& cs, std::string* error = nullptr);
    bool markUndone(std::string* error = nullptr);   // cursor--
    bool markRedone(std::string* error = nullptr);   // cursor++
    /// 최근 N 개 (cursor 기준 적용 여부 포함) — `akeir history`
    Json listJson(std::size_t limit = 20) const;
    const ChangeSet* find(std::string_view id) const;

    // ---- journal (§9.2) ----
    bool writeJournal(const ChangeSet& cs, std::string* error = nullptr);
    void clearJournal(std::string_view csId);
    std::vector<ChangeSet> pendingJournal() const;

    // ---- idempotency (§49) ----
    std::optional<Json> idempotent(std::string_view key) const;
    bool rememberIdempotent(std::string_view key, const Json& envelope, std::string* error = nullptr);

    std::string historyDir() const;
    std::string journalDir() const;

private:
    std::string root_;
    std::vector<ChangeSet> entries_;
    std::size_t cursor_ = 0;

    bool writeAll(std::string* error) const;
    bool writeCursor(std::string* error) const;
};

} // namespace pme
