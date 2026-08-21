// akeir/runtime/Project.h — authoring document model (source of truth). 설계 문서 §5 (project 구조), §5.3 (canonical 규약), §6 (entity/prefab/world 데이터),
// §7 (ID), §19 (reference graph), §29 (validation), §34 (prefab override: set/add/remove, absent = inherit), §88.2 (authoring world ≠ play world), §88.9 (파일 granularity).
//
//   Project = project.json + Worlds/*.world.json + Prefabs/*.prefab.json (+ Data/, Config/, Assets/*.meta.json — 이후 Phase)
//   모든 문서는 Json(ordered_json) 그대로 보관한다 — ChangeSet(§78)이 RFC 6901/6902 로 이 문서들을 직접 고친다.
//   Project 는 읽기·조회·검증·resolve·canonical 저장만 한다. 변경은 Phase 3 CommandBus 가 한다.
//
//   world 문서:
//     { "$schema": "game://schema/world/1", "schemaVersion": 1, "id": "world_…", "name": "TestArena",
//       "entities": { "entity_…": { "name", "parent": null|"entity_…", "order": "a0", "tags": [...],
//                                  "components": { "Transform": {...} }                  ← 비인스턴스
//                               | "prefab": "prefab_…", "set": {...}, "add": {...}, "remove": [...] } } }   ← 인스턴스
//   prefab 문서:
//     { "$schema": "game://schema/prefab/1", "schemaVersion": 1, "id": "prefab_…", "name": "Goblin", "tags": [...],
//       "components": {...}  |  "base": "prefab_…", "set": {...}, "add": {...}, "remove": [...] }
#pragma once

#include "akeir/core/Diagnostic.h"
#include "akeir/core/Id.h"
#include "akeir/core/Json.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace akeir {

constexpr int kProjectSchemaVersion = 1;

/// 문서 안의 위치 (id index 의 값)
struct DocLocation {
    std::string doc;          // 프로젝트 상대 경로 ("Worlds/TestArena.world.json")
    std::string pointer;      // RFC 6901 ("/entities/entity_…" 또는 "" = 문서 자체)
    std::string kind;         // "world" | "prefab" | "entity"
};

class Project {
public:
    /// rootDir/project.json 을 읽고 Worlds/ Prefabs/ 를 로드한다. 파싱·구조 오류는 diagnostics 로 (error 가 있어도 나머지는 로드한다).
    static std::optional<Project> load(const std::string& rootDir, std::vector<Diagnostic>& diagnostics);
    /// 빈 프로젝트 (테스트, `akeir project init`)
    static Project create(const std::string& rootDir, const std::string& name, int tickRate = 60);

    const std::string& rootDir() const { return rootDir_; }
    const Json& projectJson() const { return projectJson_; }
    Json& projectJsonMut() { return projectJson_; }   // project.json 편집 (`project init` / 이후 `project.set`). 저장은 saveAll()
    std::string name() const { return projectJson_.value("name", ""); }
    int tickRate() const { return projectJson_.value("tickRate", 60); }
    std::uint64_t seed() const { return projectJson_.value("seed", 0ULL); }
    std::optional<std::string> defaultWorld() const;   // world id

    // ---- 문서 ----
    const std::map<std::string, Json>& documents() const { return docs_; }
    const Json* document(std::string_view path) const;
    Json* documentMut(std::string_view path);               // ChangeSet 적용용 (Phase 3). 적용 후 reindex() 필요
    void setDocument(const std::string& path, Json doc);     // 새 문서 추가/교체 + reindex
    bool removeDocument(std::string_view path);              // 문서 제거 + reindex (world.delete 등). 없으면 false
    /// 문서 맵 전체 (ChangeSet applyOps 용, Phase 3). 바꾼 뒤 반드시 reindex().
    std::map<std::string, Json>& documentsMut() { return docs_; }
    void reindex();

    std::vector<std::string> worldPaths() const;
    std::vector<std::string> prefabPaths() const;
    /// id → 위치. 없으면 nullopt.
    std::optional<DocLocation> locate(std::string_view id) const;
    /// 이름 또는 path 선택자 → id 후보 (§7.4). "name:Goblin_01" / "path:TestArena/Encounter_05/Goblin_01" / id prefix
    std::vector<std::string> resolveSelector(std::string_view selector) const;
    /// entity 의 사람용 path "<worldName>/<ancestors…>/<name>" (§7.4)
    std::optional<std::string> entityPath(std::string_view entityId) const;

    // ---- prefab / entity resolve (§34) ----
    /// prefab 체인을 resolve 한 components 객체. 실패(없음/cycle/override 대상 없음)는 diagnostics 에.
    std::optional<Json> resolvePrefab(std::string_view prefabId, std::vector<Diagnostic>* diagnostics = nullptr) const;
    /// world entity 의 최종 components (인스턴스면 prefab resolve + set/add/remove, 아니면 그대로)
    std::optional<Json> resolveEntityComponents(std::string_view entityId, std::vector<Diagnostic>* diagnostics = nullptr) const;

    // ---- 저장 (§5.3 canonical) ----
    /// 문서를 §5.3 규약(헤더 키 순서, component A→Z, property 선언 순서, entity id 순)으로 정돈한 복사본
    static Json canonicalizeDocument(const Json& doc);
    /// 모든 문서를 canonical 로 디스크에 쓴다 (temp+rename). 실패한 경로 목록을 돌려준다.
    std::vector<std::string> saveAll() const;
    bool saveDocument(std::string_view path, std::string* error = nullptr) const;

    // ---- 검증 (§29) ----
    std::vector<Diagnostic> validate() const;

    // ---- reference graph (§19) ----
    struct Reference {
        std::string from;        // 참조하는 객체 id (entity/prefab/world) 또는 "project"
        std::string kind;        // "prefab" (인스턴스→prefab) | "base" (derived prefab→base) | "parent" | "property" (Ref 속성) | "defaultWorld" | "override" (set/add 값 안의 Ref)
        std::string doc;         // 문서 경로
        std::string pointer;     // 참조가 적힌 위치 (RFC 6901)
        std::string detail;      // property 면 "Component.prop", override 면 override 키
        Json toJson() const;
    };
    /// id 를 가리키는 모든 참조 (authoring 문서 기준, 런타임 Ref 제외). 정렬된 순서.
    std::vector<Reference> referencesTo(std::string_view id) const;
    /// id 가 가리키는 모든 참조 (나가는 간선)
    std::vector<Reference> referencesFrom(std::string_view id) const;

private:
    std::string rootDir_;
    Json projectJson_ = Json::object();
    std::map<std::string, Json> docs_;                 // path → document (정렬된 맵: 결정적 순회)
    std::map<std::string, DocLocation, std::less<>> index_;  // id → location
    std::map<std::string, std::vector<std::string>> duplicates_; // id → 중복 위치들 (reindex 가 채움)

    Json resolvePrefabRec(std::string_view prefabId, std::vector<std::string>& chain, std::vector<Diagnostic>* diags, bool& ok) const;
    static bool applyOverrides(Json& components, const Json& container, const std::string& docPath, const std::string& basePointer,
                               std::vector<Diagnostic>* diags, const std::string& objectId);
    void loadDirectory(const std::string& subdir, const std::string& suffix, const std::string& kind, std::vector<Diagnostic>& diags);
};

/// 문서 헤더 키의 고정 순서 (§5.3)
const std::vector<std::string>& headerKeyOrder();

} // namespace akeir
