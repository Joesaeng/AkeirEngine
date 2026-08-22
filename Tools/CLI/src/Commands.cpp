// Tools/CLI/Commands.cpp — Phase 0 명령: version, capabilities, crash-test, hang-test. 이후 Phase 에서 확장된다.
// 설계 문서 §8, §13 (exit code 표), §15 (capabilities), §47 (tools[] 15개), §88.4 (크래시/행 테스트)
#include "Commands.h"
#include "ExeInfo.h"
#include "Serve.h"
#include "akeir/commands/CommandBus.h"
#include "akeir/core/Crash.h"
#include "akeir/core/ExitCodes.h"
#include "akeir/core/FpEnv.h"
#include "akeir/core/Hash.h"
#include "akeir/core/Log.h"

#include <chrono>
#include <thread>

namespace akeir::cli {

namespace {

Envelope cmdVersion(Context& ctx) {
    Json r = Json::object();
    r["engine"] = "AKEIR";
    r["engineVersion"] = AKEIR_VERSION_STRING;
    r["release"] = "v0.1.1";
    r["fpFlagsHash"] = AKEIR_FP_FLAGS_HASH;      // §22.3 replay header / §41
    r["fpFlags"] = AKEIR_FP_FLAGS_STRING;
#if defined(_MSC_VER) && !defined(__clang__)
    r["compiler"] = "msvc " + std::to_string(_MSC_FULL_VER);
#elif defined(__clang__)
    r["compiler"] = std::string("clang ") + __clang_version__;
#else
    r["compiler"] = "gcc";
#endif
    r["designDoc"] = "AKEIR.md";
    r["fpEnv"] = fpEnvStatus().toJson();       // §22.2 FPU 환경
    r["exe"] = ownExeInfoJson();               // which build is running (ADR-0034: MCP worker restart, serve staleness)
    return Envelope::success("project.version", r);
}

Envelope cmdCapabilities(Context& ctx) {
    return Envelope::success("capabilities", capabilitiesJson());
}

Envelope cmdCrashTest(Context& ctx) {
    // §74 Phase 0 성공 기준: "강제 crash 시 minidump 경로가 envelope 에 실리고 exit 6"
    AKEIR_LOG(Warn, "cli", "crash_test", "Forcing an access violation on purpose.");
    debugForceCrash();
}

Envelope cmdHangTest(Context& ctx) {
    // watchdog 검증: --timeout 보다 오래 잔다 → TIMEOUT envelope + exit 7
    AKEIR_LOG(Warn, "cli", "hang_test", "Sleeping forever on purpose (watchdog should fire).");
    for (;;) std::this_thread::sleep_for(std::chrono::seconds(1));
}

} // namespace

const std::vector<CommandSpec>& commandTable() {
    static const std::vector<CommandSpec> table = [] {
        std::vector<CommandSpec> t = {
        {"project.version", {"version"}, "Query", "Engine version",
         "Engine version, compiler and determinism flag hash (fpFlagsHash, §22.3).", "akeir version [--json]", true, false, true, cmdVersion},
        {"capabilities", {"capabilities"}, "Query", "Capability discovery",
         "Full tool/command descriptors, exit codes and error codes (§15). MCP tools/list is a pass-through of result.tools.",
         "akeir capabilities [--json]", true, false, true, cmdCapabilities},
        {"debug.crash_test", {"debug", "crash-test"}, "RuntimeControl", "Force a crash",
         "Forces an access violation to verify the crash handler writes a minidump and a CRASH envelope (exit 6, §88.4).",
         "akeir debug crash-test", false, false, false, cmdCrashTest},
        {"debug.hang_test", {"debug", "hang-test"}, "RuntimeControl", "Force a hang",
         "Sleeps forever to verify the watchdog emits a TIMEOUT envelope (exit 7, §88.4). Use with --timeout 2s.",
         "akeir debug hang-test --timeout 2s", false, false, false, cmdHangTest},
        };
        registerProjectCommands(t);
        registerInitCommands(t);
        registerRunCommands(t);
        registerMutationCommands(t);
        registerTestCommands(t);
        registerSdlCommands(t);
        registerServeCommands(t);
        registerMcpCommands(t);
        return t;
    }();
    return table;
}

const CommandSpec* findCommand(const std::vector<std::string>& positionals, std::size_t& consumed) {
    const CommandSpec* best = nullptr;
    consumed = 0;
    for (const auto& spec : commandTable()) {
        if (spec.cli.size() > positionals.size()) continue;
        bool match = true;
        for (std::size_t i = 0; i < spec.cli.size(); ++i) if (spec.cli[i] != positionals[i]) { match = false; break; }
        if (match && spec.cli.size() > consumed) { best = &spec; consumed = spec.cli.size(); }
    }
    return best;
}

/// §12 envelope 의 JSON Schema (자기완결 — MCP outputSchema 로 그대로 쓴다; $ref 없음)
Json envelopeSchema() {
    Json diag = Json{{"type", "object"}, {"properties", Json{{"ruleId", Json{{"type", "string"}}}, {"level", Json{{"type", "string"}}}, {"message", Json{{"type", "object"}, {"properties", Json{{"text", Json{{"type", "string"}}}}}}},
                                                           {"physical", Json{{"type", "object"}}}, {"logical", Json{{"type", "object"}}}, {"fixes", Json{{"type", "array"}}}, {"fingerprint", Json{{"type", "string"}}}}}};
    Json err = diag;
    err["properties"]["category"] = Json{{"type", "string"}, {"enum", Json::array({"usage", "validation", "not_found", "conflict", "precondition", "crash", "timeout", "internal", "cancelled"})}};
    err["properties"]["retryable"] = Json{{"type", "boolean"}};
    err["properties"]["details"] = Json{{"type", "object"}};
    return Json{{"$schema", "https://json-schema.org/draft/2020-12/schema"}, {"type", "object"},
                {"properties", Json{{"ok", Json{{"type", "boolean"}}}, {"command", Json{{"type", "string"}}}, {"result", Json{{"type", "object"}, {"description", "present when ok"}}},
                                    {"error", err}, {"changes", Json{{"type", "array"}, {"description", "ChangeSet ops (RFC 6902 superset: op/doc/path/value/before)"}}},
                                    {"warnings", Json{{"type", "array"}, {"items", diag}}}, {"meta", Json{{"type", "object"}}}}},
                {"required", Json::array({"ok", "command", "changes", "warnings", "meta"})}};
}

Json capabilitiesJson() {
    Json r = Json::object();
    r["info"] = Json{{"title", "AKEIR Engine Command API"}, {"engine", "AKEIR"}, {"version", AKEIR_VERSION_STRING}, {"sdl", sdlAvailable()}, {"exe", ownExeInfoJson()}};

    // §47 tools[] — MCP 에 노출되는 15개 (이름 = command id 의 '.' → '_'). 구현된 것만 enabled:true. tools/list 는 enabled 만 pass-through 한다.
    // inputSchema 는 CLI 인자와 같은 의미의 JSON. apply.changes[].op 의 oneOf 는 busCommands[] 의 args 스키마다.
    auto tool = [](const char* name, const char* title, const char* desc, Json input, bool readOnly, bool destructive, bool idempotent, const char* cli, bool enabled) {
        Json t = Json::object();
        t["name"] = name; t["title"] = title; t["description"] = desc;
        if (!input.contains("type")) input["type"] = "object";
        t["inputSchema"] = input;
        t["outputSchema"] = envelopeSchema();
        t["annotations"] = Json{{"readOnlyHint", readOnly}, {"destructiveHint", destructive}, {"idempotentHint", idempotent}, {"openWorldHint", false}};
        t["cli"] = cli;
        t["enabled"] = enabled;
        return t;
    };
    auto props = [](std::initializer_list<std::pair<const char*, Json>> ps) { Json o = Json::object(); for (auto& [k, v] : ps) o[k] = v; return Json{{"properties", o}, {"additionalProperties", false}}; };
    Json S = Json{{"type", "string"}}, B = Json{{"type", "boolean"}}, I = Json{{"type", "integer"}};
    const char* kSel = "selector: an id (entity_…/prefab_…/world_…), a bare name (Goblin_01), name:<name>, or path:<World>/<Parent>/<Child>. Resolves entities AND prefabs; ambiguous → AMBIGUOUS_SELECTOR with candidates";
    Json SEL = Json{{"type", "string"}, {"description", kSel}};
    Json tools = Json::array();
    tools.push_back(tool("capabilities", "Capability discovery", "Full tool/command descriptors, exit codes and error codes (§15).", props({}), true, false, true, "akeir capabilities --json", true));
    tools.push_back(tool("project_info", "Project summary", "Name, tickRate, seed, worlds, prefabs, registered components.", props({}), true, false, true, "akeir project info --json", true));
    tools.push_back(tool("schema_describe", "Component schemas", "JSON Schema 2020-12 (+x-*) and wire format of components (§14).", props({{"component", S}, {"all", B}}), true, false, true, "akeir schema component <Name> --json", true));
    tools.push_back(tool("query", "Query play world", "Build the world, run N ticks, return entities matching with/without (components or #tags) (§16).", props({{"with", Json{{"type", "array"}, {"items", S}, {"description", "component names the entity must have; prefix # for tags (#enemy)"}}}, {"without", Json{{"type", "array"}, {"items", S}, {"description", "component names / #tags to exclude"}}}, {"ticks", Json{{"type", "integer"}, {"description", "simulate this many ticks first (default 0)"}}}, {"components", Json{{"type", "boolean"}, {"description", "include each entity's full component values"}}}, {"limit", Json{{"type", "integer"}, {"description", "max rows (default 100); raise it to see more — there is no cursor yet"}}}, {"world", S}}), true, false, true, "akeir query --with EnemyAI --ticks 300 --json", true));
    tools.push_back(tool("inspect", "Inspect an entity", "Resolved components of one entity after N ticks (play world) — `akeir dump` (§25).", props({{"entity", SEL}, {"ticks", Json{{"type", "integer"}, {"description", "simulate this many ticks first (default 0)"}}}, {"world", S}}), true, false, true, "akeir dump <selector> --ticks 600 --json", true));
    tools.push_back(tool("explain", "Explain an authoring object", "Where it lives, prefab chain, overrides, children, resolved components, lifecycle (§18).", props({{"selector", SEL}}), true, false, true, "akeir explain <selector> --json", true));
    tools.push_back(tool("refs", "Reference graph", "Who references this id and what it references: prefab instances, parent links, Ref properties, overrides, defaultWorld (§19).", props({{"selector", SEL}}), true, false, true, "akeir refs <selector> --json", true));
    tools.push_back(tool("apply", "Apply a batch of commands", "Atomic batch of Mutation commands (§49). changes[].op ∈ busCommands[].id; '$name' refers to an earlier change's result (as: name). dryRun returns the ChangeSet without writing. Response.changes = RFC 6902 superset ops (§78).",
                         props({{"changes", Json{{"type", "array"}, {"description", "each item: {op: <busCommands[].id>, ...args of that command, as?: name}. Call the `capabilities` tool for busCommands[] (ids + arg schemas)."}, {"items", Json{{"type", "object"}, {"required", Json::array({"op"})}}}}}, {"atomic", Json{{"type", "boolean"}, {"description", "default true: all-or-nothing, one undo step"}}}, {"dryRun", Json{{"type", "boolean"}, {"description", "compute the ChangeSet without writing"}}}, {"idempotencyKey", S}, {"tx", Json{{"type", "string"}, {"description", "open tx handle from the tx tool"}}}}), false, false, false, "akeir apply batch.json --json", true));
    tools.push_back(tool("validate", "Validate project data", "All §29 rules; fix:true applies MachineApplicable fixes via CommandBus (undoable).", props({{"fix", B}, {"dryRun", B}}), true, false, true, "akeir validate [--fix] --json", true));
    tools.push_back(tool("run", "Run headless", "Fixed-tick deterministic run; finalHash, per-N hashes, snapshot (§20, §22).", props({{"ticks", I}, {"seed", I}, {"world", S}, {"hashEvery", I}, {"snapshotOut", S}}), false, false, true, "akeir run --headless --ticks 600 --json", true));
    tools.push_back(tool("run_status", "Run handle status", "Result of a run started in this `akeir serve` session (run.start returns result.run) (§46.2).", props({{"run", S}}), true, false, true, "akeir run status <run_id> --json", true));
    tools.push_back(tool("test", "Run data-driven tests", "Tests/**/*.test.json scenarios: setup, scripted inputs, snapshot assertions (always/eventually/at), run-twice determinism; results.json + JUnit (§23, §24).", props({{"filter", S}, {"junit", S}, {"resultsDir", S}}), true, false, true, "akeir test [filter] --json", true));
    tools.push_back(tool("capture", "Capture a frame", "Software-rasterized PNG of the world after N ticks (no GPU; deterministic). compare: golden comparison with tolerance (§27, §27.1).", props({{"ticks", I}, {"width", I}, {"height", I}, {"out", S}, {"compare", S}}), true, false, true, "akeir capture --ticks 60 --out Cache/capture/f.png --json", sdlAvailable()));
    tools.push_back(tool("tx", "Multi-call transaction", "begin/commit/rollback/list: opaque tx handle + TTL (10 min); pass tx to apply to group several calls into one undo step. Works directly inside an MCP session; from the CLI it needs `akeir serve`.", props({{"action", Json{{"enum", Json::array({"begin", "commit", "rollback", "list"})}}}, {"tx", S}, {"ttl", I}}), false, false, false, "akeir tx begin|commit|rollback|list", true));
    tools.push_back(tool("history", "Undo / redo / list", "History of ChangeSets (§10). undo applies inverse(ops); actor filter; conflicts reported.", props({{"action", Json{{"enum", Json::array({"undo", "redo", "list"})}}}, {"steps", I}, {"actor", S}, {"limit", I}}), false, false, false, "akeir undo|redo|history", true));
    r["tools"] = tools;

    // busCommands[] — CommandBus 의 Mutation command (apply.changes[].op 의 oneOf). 스키마는 Engine/Commands/BuiltinCommands.cpp 가 선언한다.
    {
        Project scratch = Project::create("", "capabilities");
        BusOptions bo; bo.persist = false;
        CommandBus bus(scratch, bo);
        r["busCommands"] = bus.commandsJson();
    }

    Json cmds = Json::array();
    for (const auto& spec : commandTable()) {
        std::string cliLine = "akeir";
        for (const auto& p : spec.cli) cliLine += " " + p;
        cmds.push_back(Json{{"id", spec.id}, {"kind", spec.kind}, {"title", spec.title}, {"description", spec.description},
                            {"cli", spec.usage.empty() ? cliLine : spec.usage},
                            {"annotations", Json{{"readOnlyHint", spec.readOnly}, {"destructiveHint", spec.destructive},
                                                 {"idempotentHint", spec.idempotent}, {"openWorldHint", false}}}});
    }
    r["commands"] = cmds;

    Json exitCodes = Json::object();
    for (int code : {kExitOk, kExitCommandFailed, kExitUsage, kExitFindings, kExitConfirmationRequired, kExitNotFound, kExitCrash, kExitTimeout, kExitInterrupted, kExitTerminated})
        exitCodes[std::to_string(code)] = exitCodeDescription(code);
    r["exitCodes"] = exitCodes;
    r["errorCodes"] = Json::array({"USAGE_ERROR", "UNKNOWN_COMMAND", "PROJECT_NOT_FOUND", "CRASH", "TIMEOUT", "CONFIRMATION_REQUIRED", "INTERNAL_ERROR",
                                "VALIDATION_FAILED", "ENTITY_NOT_FOUND", "PREFAB_NOT_FOUND", "WORLD_NOT_FOUND", "AMBIGUOUS_SELECTOR", "COMPONENT_UNKNOWN", "COMPONENT_EXISTS",
                                "COMPONENT_NOT_ON_ENTITY", "COMPONENT_DEPENDENCY", "COMPONENT_REQUIRED", "PROPERTY_UNKNOWN", "PROPERTY_RUNTIME_ONLY", "PROPERTY_READ_ONLY",
                                "PROPERTY_TYPE_MISMATCH", "PROPERTY_OUT_OF_RANGE", "ENUM_VALUE_INVALID", "HIERARCHY_CYCLE", "ENTITY_HAS_CHILDREN", "DOCUMENT_EXISTS",
                                "BASE_MISMATCH", "UNDO_CONFLICT", "REDO_CONFLICT", "UNDO_ACTOR_MISMATCH", "NOTHING_TO_UNDO", "NOTHING_TO_REDO", "TX_UNKNOWN_OR_EXPIRED", "TX_REQUIRES_SERVE",
                                "RUN_UNKNOWN_OR_EXPIRED", "RUN_STATUS_REQUIRES_SERVE", "SERVE_NOT_RUNNING", "SERVE_ALREADY_RUNNING", "FEATURE_UNAVAILABLE", "TEST_FAILED", "TESTS_NOT_FOUND", "CAPTURE_MISMATCH", "CAPTURE_FAILED",
                                "APPLY_INVALID", "APPLY_BAD_REFERENCE", "CHANGESET_BEFORE_MISMATCH", "CHANGESET_APPLY_FAILED", "JOURNAL_WRITE_FAILED", "SAVE_FAILED",
                                "REFLECT_MEMBER_UNLISTED", "MCP_WORKER_RESTARTED", "SERVE_STALE_EXE",
                                "ASSET_META_INVALID", "ASSET_SOURCE_MISSING", "ASSET_SOURCE_INVALID", "ASSET_SUBASSET_RECT_INVALID", "ASSET_SUBASSET_MISSING"});
    return r;
}

} // namespace akeir::cli
