# Engine/Serialization (`akeir_serialization`)

§5.3 canonical JSON and reflection-driven component ↔ JSON conversion.

| Header | § | Provides |
|---|---|---|
| `akeir/serialization/Canonical.h` | §5.3, §9.2, RFC 8785 | `canonicalDump` (2-space indent, scalar arrays on one line, no `.0` on integers, NaN rejected), `jcsDump` (for hashing), `writeCanonicalFile` (temp+rename), `readJsonFile`/`parseJson` (comments and trailing commas rejected, byte offsets reported), `canonicalizeFloat(s)` (float32 shortest round-trip), `isCanonicalText` |
| `akeir/serialization/ComponentJson.h` | §14, §26.1, §29, §79, §88.8 | `componentToJson(meta, ptr, Visibility)`, `validateComponentJson` → Diagnostic[] (PROPERTY_UNKNOWN / PROPERTY_TYPE_MISMATCH / PROPERTY_OUT_OF_RANGE (+clamp fix) / PROPERTY_OUT_OF_WARN_RANGE / PROPERTY_NOT_MULTIPLE / ENUM_VALUE_INVALID (+candidate fix) / REF_FORMAT_INVALID / RUNTIME_ONLY_IN_AUTHORING / PROPERTY_REQUIRED_MISSING), `componentFromJson` (applies nothing if there is an error) |

Rule: project files are written **only through this writer**. Never write them with `Json::dump()` directly. Document-level key order (header order, components A→Z, entities by id) is produced by `Project::canonicalizeDocument` — `canonicalDump` preserves whatever order it is given.

The `required` check applies only to authoring visibility (snapshot/save are partial projections of runtime state).
