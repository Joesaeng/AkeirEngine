# Engine/Commands (`pme_commands`)

**프로젝트의 유일한 쓰기 경로.** CLI·Editor·MCP·`validate --fix` 는 전부 `CommandBus` 를 호출하고, 결과는 항상 `ChangeSet`(§78) 으로 남는다.
설계 문서 §8 (CommandBus / CommandKind / ChangeBuilder), §8.1 (command id), §9 (Transaction, commit 절차), §10 (Undo/History), §49 (apply), §50 (dry-run), §78 (ChangeSet), §78.1 (Command → ops 매핑).

```
Tools (CLI / Editor / MCP)
   │  execute(id, args) · apply(batch) · undo/redo · beginTx/commitTx
   ▼
CommandBus ──▶ fork(Project 복사) ──▶ handler(CommandContext) ──▶ ChangeBuilder(ops, before 자동) ──▶ validateFork(새 오류만 거부)
   │                                                                                                   │
   │  dryRun ───────────────────────────────────────────────────────────────────────────────────────────┘→ envelope.changes (저장 없음)
   ▼
commit(cs): base 검사 → Cache/journal/<cs>.json → project 에 applyOps → touched 문서 canonical temp+rename → Cache/history/history.jsonl → journal 삭제
```

## 파일

| 파일 | § | 내용 |
|---|---|---|
| `include/pme/commands/ChangeSet.h`, `src/ChangeSet.cpp` | §78, §10.1, §9.2 | `ChangeOp{op, doc, path, from, value, before, blob, beforeBlob}`, `ChangeSet{id, tx, actor, createdAt, intent, base, ops, touched, lossy, diagnostics}`, `toJson/fromJson`, `inverse()`, `compose()`, `applyOps(docs, ops)`, `escapeToken/unescapeToken` |
| `include/pme/commands/History.h`, `src/History.cpp` | §9.2, §10, §49 | `Cache/history/history.jsonl`(append-only) + `cursor.json` + `idempotency.json`, `Cache/journal/<cs>.json`(write-ahead) |
| `include/pme/commands/CommandBus.h`, `src/CommandBus.cpp` | §8, §9, §10, §49, §50 | `CommandKind`, `ChangeBuilder`, `CommandContext`(fail/resolveEntity/resolvePrefab/resolveWorldDoc), `CommandDef`, `CommandBus`(execute/apply/tx/undo/redo/recoverJournal), `nextOrderKey` |
| `src/BuiltinCommands.cpp` | §8, §34, §78.1 | 내장 Mutation command 13개 + 각 args JSON Schema |

## Command 목록 (`akeir capabilities --json` → `result.busCommands[]`)

| id | args | 비고 |
|---|---|---|
| `entity.create` | `{world?, name, parent?, order?, tags?, components?, prefab?, set?}` | Transform 없으면 자동 추가, 누락 prop 은 default. `prefab` 주면 인스턴스 |
| `entity.delete` | `{entity, recursive?=true}` | 자손까지. inverse 는 부모부터 되살림 |
| `entity.rename` / `entity.reparent` | `{entity, name}` / `{entity, parent\|null, order?}` | reparent 는 cycle 거부(`HIERARCHY_CYCLE`) |
| `component.add` / `component.remove` | `{entity, component, value?}` / `{entity, component}` | **`entity` 는 prefab selector 도 받는다** (prefab 편집 = 모든 인스턴스). remove 는 의존 component 가 있으면 거부(`COMPONENT_DEPENDENCY`), Transform 불가 |
| `property.set` | `{entity, component, path, value}` | `path` = component 안 JSON Pointer(`max`, `/position/0`). reflection 으로 type/range/enum 검증. runtimeOnly/readOnly 거부 |
| `tag.add` / `tag.remove` | `{entity, tag}` | |
| `prefab.create` | `{name, components?, base?, set?, add?, remove?, tags?}` | `Prefabs/<Name>.prefab.json` 생성 |
| `prefab.instantiate` | `{prefab, world?, name?, parent?, position?, set?, tags?}` | `entity.create` 의 sugar |
| `world.create` | `{name}` | `Worlds/<Name>.world.json` (빈 entities) |
| `document.patch` | `{doc, ops:[RFC 6902]}` | 원시 편집 escape hatch. `validate --fix` 의 `artifactChanges` 가 이 경로. copy 금지 |

selector 는 §7.4: id / id prefix / `name:X` / `path:World/Parent/Child`. 모호하면 `AMBIGUOUS_SELECTOR` + candidates.

## 인스턴스·derived prefab 편집 규칙 (§78.1)

대상이 prefab 인스턴스(`prefab` 키) 또는 derived prefab(`base` 키)이면 component/property 명령은 **문서의 `set`/`add`/`remove` 맵**만 고친다:

| 명령 | 대상이 base 에 있음 | 대상이 `add` 맵에 있음 | 없음 |
|---|---|---|---|
| `property.set` | `set["/components/C/p"] = v` — **base 값과 같으면 set 항목을 삭제** (`override: "inherited"`) | `add["/components/C"]` 안을 직접 수정 (`override: "add"`) | `COMPONENT_NOT_ON_ENTITY` |
| `component.add` | `COMPONENT_EXISTS` (단, `remove` 목록에 있으면 거기서 빼고 value 는 set 으로) | `COMPONENT_EXISTS` | `add["/components/C"] = defaults+value` |
| `component.remove` | `remove[] += "/components/C"` + 그 component 의 `set` 항목 정리 | `add` 에서 삭제 | `COMPONENT_NOT_ON_ENTITY` |

## 실행 모델 세부

- **fork**: `Project` 는 값 타입이라 복사한다. handler 는 fork 의 `ChangeBuilder` 로만 바꾼다 — builder 가 op 를 즉시 적용하고 `reindex()` 하므로 handler 는 자기 변경을 `locate()`/`resolveSelector()` 로 바로 본다.
- **ChangeBuilder 규칙**: `set()` 은 존재하면 replace, 없으면 add, 같은 값이면 op 없음(no-op 이면 commit 도 안 함). `remove/replace/move` 는 `before` 를 현재 값에서 채운다. `"/arr/-"`(append) 는 역연산이 안 되므로 **구체 인덱스로 바꿔 기록**한다.
- **commit 전 검증**: `validateFork()` 가 fork 에서 `Project::validate()` 를 돌려 **새로 생긴 error**(fingerprint 가 baseline 에 없는 것)만 거부한다 → `VALIDATION_FAILED` + `details.diagnostics`. `ExecOptions.validateAfter=false`(CLI `--no-validate`) 로 끌 수 있다. `validate --fix` 는 끈다(오류를 고치는 중이므로).
- **base 검사(optimistic concurrency)**: bus 생성 시 문서별 `sha256(JCS(canonical(doc)))` 를 기억하고 commit 때 디스크 파일을 다시 읽어 비교 → 다르면 `BASE_MISMATCH`(conflict). `ChangeSet.base` 에 기록.
- **before 비교**: `applyOps` 는 `before` 와 현재 값을 JCS 로 비교한다(키 순서 무시). 파일로 갔다 온 문서는 §5.3 순서로 재정렬되어 `ordered_json ==` 가 실패하기 때문.
- **history**: 선형 스택 + cursor. undo = `inverse(ops)` 를 commit 경로로 적용하고 cursor--, redo = 원본 ops 재적용 + cursor++ (둘 다 history 에 push 하지 않음). 새 commit 은 redo 꼬리를 버린다(그때만 history.jsonl 을 다시 쓴다). `--actor X` 는 최근 항목이 다른 actor 면 `UNDO_ACTOR_MISMATCH`.
- **tx**: `beginTx(ttlMs)` 가 fork 를 잡고 TTL 있는 opaque handle 을 준다; `execute(…, {tx})` 가 그 위에 누적, `commitTx()` 가 `ChangeSet::compose` 로 하나의 history 항목. 만료/미지 handle 은 `TX_UNKNOWN_OR_EXPIRED` (§9.1). `txInfo/txList/expireTransactions`. one-shot CLI 는 `apply` 내부에서만, `akeir serve` 는 CLI 호출에 걸친 tx 로 쓴다.
- **apply (§49)**: `{atomic?=true, dryRun?, idempotencyKey?, changes:[{op, ...args, as?}]}`. `"$name"` → 그 change 의 `result.id`(없으면 result 전체), `"$name.field"` → 필드, `"$$x"` → 리터럴. atomic 이면 tx 하나로, 실패 시 `details.index/op/results`. idempotencyKey 는 `Cache/history/idempotency.json` 에 응답을 저장해 재실행 없이 돌려준다(`meta.idempotentReplay`).
- **journal 복구 (§9.2)**: `recoverJournal()` — 남은 journal 의 touched 문서가 전부 base 해시면 ops 를 다시 적용·저장(`completed`), 전부 달라졌으면 이미 써진 것으로 보고 history 에만 추가(`already-applied`), 섞여 있으면 `partial` 로 남긴다. CLI 는 모든 쓰기 명령 전에 호출한다.
- **persist=false**(`BusOptions`) 면 파일/journal/history 를 쓰지 않는다 (테스트, capabilities 의 스키마 나열).

## 오류 코드 (category)

`ARG_REQUIRED`/`ARG_TYPE`/`AMBIGUOUS_SELECTOR`/`WORLD_REQUIRED`/`PARENT_IN_OTHER_WORLD`(usage) · `ENTITY_NOT_FOUND`/`PREFAB_NOT_FOUND`/`WORLD_NOT_FOUND`/`COMPONENT_UNKNOWN`/`COMPONENT_NOT_ON_ENTITY`/`PROPERTY_UNKNOWN`/`TAG_NOT_ON_ENTITY`/`TX_UNKNOWN_OR_EXPIRED`/`DOCUMENT_NOT_FOUND`(not_found) · `COMPONENT_EXISTS`/`DOCUMENT_EXISTS`/`BASE_MISMATCH`/`UNDO_CONFLICT`/`REDO_CONFLICT`/`UNDO_ACTOR_MISMATCH`/`CHANGESET_APPLY_FAILED`(conflict) · `PROPERTY_RUNTIME_ONLY`/`PROPERTY_READ_ONLY`/`COMPONENT_REQUIRED`/`COMPONENT_DEPENDENCY`/`HIERARCHY_CYCLE`/`ENTITY_HAS_CHILDREN`/`NOTHING_TO_UNDO`/`NOTHING_TO_REDO`(precondition) · `VALIDATION_FAILED` + reflection 규칙(`PROPERTY_TYPE_MISMATCH`, `PROPERTY_OUT_OF_RANGE`, `ENUM_VALUE_INVALID` …)(validation) · `APPLY_INVALID`/`APPLY_BAD_REFERENCE`(usage) · `JOURNAL_WRITE_FAILED`/`SAVE_FAILED`/`HISTORY_WRITE_FAILED`/`INTERNAL_ERROR`(internal).

## 테스트

`Tests/Commands_ChangeSet.cpp`(ops/inverse/compose/escape), `Tests/Commands_Bus.cpp`(샘플 Game/ 을 임시 디렉터리로 복사해 commit·파일 canonical·undo byte-identical·history 영속·actor 필터·BASE_MISMATCH·인스턴스 override·prefab 편집·entity/prefab/world 생성·dry-run·검증 거부·tx·apply·idempotency·journal 복구).

## 미구현 / 다음

- `file.*` op (바이너리 asset, §78 규칙 4) — asset store 가 생기는 Phase 2+ 까지 `applyOps` 가 거부한다.
- `project.json` 은 문서 맵 밖이라 command 로 바꿀 수 없다 (`defaultWorld` 변경 등 → Phase 4).
- `--if-match <cs>`(dry-run 결과를 그대로 commit, §50), checkpoint(§52), semantic diff 출력(§51), rename table/migration(§53).
- `apply` 의 changes[] 를 `busCommands[].args` 스키마로 사전 검증하는 단계(지금은 handler 가 직접 검사).
- Query 계열 command(`entity.get`, `query`)는 아직 CLI 가 Project/PlayWorld 를 직접 부른다 — bus 의 `CommandKind::Query` 로 옮기는 것은 `akeir serve` 와 함께.
