# Engine/Serialization (`akeir_serialization`)

§5.3 canonical JSON 과 reflection 기반 component ↔ JSON.

| 헤더 | § | 제공 |
|---|---|---|
| `akeir/serialization/Canonical.h` | §5.3, §9.2, RFC 8785 | `canonicalDump`(2-space, 스칼라 배열 한 줄, 정수에 `.0` 없음, NaN 거부), `jcsDump`(해시용), `writeCanonicalFile`(temp+rename), `readJsonFile`/`parseJson`(주석·trailing comma 거부, byte 위치), `canonicalizeFloat(s)`(float32 shortest round-trip), `isCanonicalText` |
| `akeir/serialization/ComponentJson.h` | §14, §26.1, §29, §79, §88.8 | `componentToJson(meta, ptr, Visibility)`, `validateComponentJson` → Diagnostic[] (PROPERTY_UNKNOWN / PROPERTY_TYPE_MISMATCH / PROPERTY_OUT_OF_RANGE(+clamp fix) / PROPERTY_OUT_OF_WARN_RANGE / PROPERTY_NOT_MULTIPLE / ENUM_VALUE_INVALID(+후보 fix) / REF_FORMAT_INVALID / RUNTIME_ONLY_IN_AUTHORING / PROPERTY_REQUIRED_MISSING), `componentFromJson`(error 가 있으면 적용하지 않음) |

규칙: 프로젝트 파일은 **이 writer 로만** 쓴다. `Json::dump()` 로 직접 쓰지 않는다. 문서 수준의 키 순서(헤더 순서, component A→Z, entity id 순)는 `Project::canonicalizeDocument` 가 만든다 — `canonicalDump` 는 받은 순서를 그대로 쓴다.

required 검사는 authoring visibility 에만 적용된다 (snapshot/save 는 runtime 상태의 부분 적용).
