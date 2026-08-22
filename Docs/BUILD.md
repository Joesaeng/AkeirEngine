# BUILD — 빌드 · 테스트 · 실행

설계 문서 §41 (Build 구조), §22.2 (결정론 플래그), §88.5 (컴파일러). 결정: [DECISIONS.md](DECISIONS.md) ADR-0004~0007.

## 요구 사항 (이 저장소가 만들어진 머신 기준, 2026-08-21)

| 항목 | 확인된 값 | 비고 |
|---|---|---|
| OS | Windows 11 | |
| Visual Studio | 2022 Community 17.14 (MSVC 14.44, toolset 19.44) | "C++ 데스크톱 개발" 워크로드. **VS2022 이상 필수** (`/fp:precise` 의 FMA 규칙, §22.2) |
| CMake / Ninja | VS 번들 CMake 3.31.6, Ninja 1.12.1 | PATH 에 없어도 된다 — `scripts/build.cmd` 가 vcvars64 로 PATH 에 넣는다 |
| Windows SDK | 10.0.26100 | dbghelp (minidump) |
| git | 2.52 | 의존성 clone |
| (선택) clang / clang-cl | 22.1, `C:\Program Files\LLVM` | preset `clang-cl-debug` |
| (선택) Python | 3.14 | 보조 스크립트 |

## 한 줄 빌드

```bash
scripts\build.cmd msvc-headless all
```

= `cmake --preset msvc-headless` + `cmake --build --preset msvc-headless`.
`msvc-headless` 는 SDL3 를 빌드하지 않는 빠른 구성(창/입력 없음). 전체 구성은 `msvc-debug`.

`scripts/build.cmd [preset] [configure|build|test|all] [target]` — 예:

```bash
scripts\build.cmd msvc-debug all              # SDL3 포함 전체 (첫 빌드 수 분)
scripts\build.cmd msvc-headless build game    # 특정 타깃만
scripts\build.cmd msvc-headless test          # ctest
scripts\build.cmd clang-cl-debug all          # 두 번째 컴파일러
```

PowerShell 에서는 `scripts\build.ps1 -Preset msvc-headless [-Target game] [-Test] [-Clean]` 도 같은 일을 한다.

직접 cmake 를 부르려면 먼저 VS 환경을 잡아야 한다:

```bat
"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake --preset msvc-headless
cmake --build --preset msvc-headless
ctest --preset msvc-headless
```

## 산출물

| 경로 | 내용 |
|---|---|
| `build/<preset>/bin/akeir.exe` | CLI (§11). `msvc-release` 는 static CRT(`/MT`) 라 VC++ 재배포 없이 다른 PC 에서 돈다 — 릴리즈 zip 의 `bin/akeir.exe` |
| `build/<preset>/Tests/akeir_tests.exe` | doctest 단위 테스트 |
| `build/<preset>/compile_commands.json` | clangd 등용 |

## 테스트

```bash
build\msvc-headless\Tests\akeir_tests.exe              # 전체
build\msvc-headless\Tests\akeir_tests.exe -tc="Id*"    # 이름 필터
build\msvc-headless\Tests\akeir_tests.exe --list-test-cases
scripts\build.cmd msvc-headless test                 # ctest 경유 (doctest_discover_tests 로 케이스별 등록)
```

테스트 파일은 `Tests/<모듈>_<주제>.cpp`. 각 파일 첫 줄 주석에 관련 설계 § 를 적는다. **TEST_CASE 이름에 `;` 금지** — `doctest_discover_tests` 가 CMake 목록 구분자로 쪼개 그 테스트를 조용히 건너뛴다(v0.1.0 검증에서 발견). 정본은 `akeir_tests.exe` 직접 실행.

## CLI 스모크 테스트 (Phase 0 성공 기준, §74)

```bash
build\msvc-headless\bin\akeir.exe version --json            # exit 0, envelope
build\msvc-headless\bin\akeir.exe capabilities --json       # tools[] / commands[] / exitCodes
build\msvc-headless\bin\akeir.exe nope                      # exit 2, UNKNOWN_COMMAND
build\msvc-headless\bin\akeir.exe debug crash-test          # exit 6, Cache/crash/*.dmp, error.details.minidump
build\msvc-headless\bin\akeir.exe debug hang-test --timeout 2s   # exit 7 after 2s
```

stdout 이 TTY 가 아니면 JSON 한 줄(envelope), TTY 면 pretty JSON. `--json` 으로 강제. 로그는 stderr(JSONL).

## 새 프로젝트 만들기

```bash
build\msvc-headless\bin\akeir.exe project init MyGame --dir C:\work\MyGame --json   # project.json, Worlds/Main.world.json(MainCamera), Config/input.json, README
cd C:\work\MyGame
C:\Project\Project_ME\build\msvc-headless\bin\akeir.exe prefab create Hero --components "{\"Collider2D\":{\"shape\":\"circle\"},\"RigidBody2D\":{\"type\":\"dynamic\",\"gravityScale\":0},\"Movement\":{\"speed\":5},\"PlayerController\":{}}" --json
C:\Project\Project_ME\build\msvc-headless\bin\akeir.exe prefab instantiate name:Hero --name Player --json
C:\Project\Project_ME\build\msvc-headless\bin\akeir.exe run --headless --ticks 600 --json
```
사용할 수 있는 component 는 `akeir schema --all --json` (내장 5종 + 샘플 게임의 Health/Movement/PlayerController/EnemyAI — CLI 가 `Game/Source` 를 정적으로 링크하므로 새 프로젝트에서도 보인다). 새 component/system 은 C++ 로 추가하고 다시 빌드해야 한다 (`Game/Source/README.md`).

## 샘플 프로젝트로 Phase 1 확인 (§71 시나리오의 headless 부분)

```bash
cd Game
..\build\msvc-headless\bin\akeir.exe project info --json
..\build\msvc-headless\bin\akeir.exe validate --json                 # ok:true; 오류면 exit 3 + fixes
..\build\msvc-headless\bin\akeir.exe fmt --dry-run --json            # canonical 이 아닌 파일 목록
..\build\msvc-headless\bin\akeir.exe schema component Health --json   # JSON Schema 2020-12 + wireFormat
..\build\msvc-headless\bin\akeir.exe explain name:Goblin_03 --json    # prefab 체인, overrides, resolved components, lifecycle
..\build\msvc-headless\bin\akeir.exe refs name:Goblin --json           # 누가 이 prefab 을 쓰는가 (§19)
..\build\msvc-headless\bin\akeir.exe run --headless --ticks 600 --hash-every 100 --snapshot-out Cache\snap.json --json
..\build\msvc-headless\bin\akeir.exe query --with EnemyAI --ticks 300 --components --json
..\build\msvc-headless\bin\akeir.exe dump path:Arena/Player --ticks 600 --json
```
`run` 을 두 번 실행하면 `result.finalHash` 가 같아야 한다 (T0, §22.1). 다르면 결정론 버그다.

## 쓰기 명령으로 Phase 3 확인 (§8–§10, §49, §50) — 샘플을 복사해서 해 본다

```bash
xcopy /E /I Game C:\tmp\akeir_demo && cd C:\tmp\akeir_demo
set G=C:\Project\Project_ME\build\msvc-headless\bin\akeir.exe
%G% set name:Goblin_01 Health.max 45 --json                 # 인스턴스 → set override. changes[0].op = add …/set/~1components~1Health~1max
%G% set name:Goblin Movement.speed 4.5 --json               # prefab 편집 → 모든 고블린 (ADR-0021)
%G% entity create Crate --parent path:TestArena/Arena --components "{\"Collider2D\":{\"shape\":\"box\"}}" --json
%G% component add name:Crate RigidBody2D --value "{\"type\":\"dynamic\"}" --json
%G% prefab create Barrel --components "{\"Collider2D\":{\"shape\":\"circle\",\"radius\":0.4}}" --json
%G% prefab instantiate name:Barrel --parent path:TestArena/Arena --position 3,1,0 --json
%G% apply batch.json --json                                 # {"changes":[{"op":"prefab.create","as":"b",…},{"op":"prefab.instantiate","prefab":"$b",…}]}
%G% run --headless --ticks 300 --json                       # 바뀐 world 로 실행
%G% history --json                                          # Cache/history 의 ChangeSet 목록 + cursor
%G% undo 2 --json                                           # inverse(ops) 적용; 파일이 byte-identical 로 돌아간다
%G% redo --json
%G% entity delete name:Crate --dry-run --json               # 파일 안 건드림, changes[] 만
%G% validate --fix --json                                   # MachineApplicable fix 적용 (history 에 actor cli:validate-fix 로 남는다)
%G% cmd document.patch --args "{\"doc\":\"Prefabs/Goblin.prefab.json\",\"ops\":[{\"op\":\"replace\",\"path\":\"/components/Health/max\",\"value\":35}]}" --json
```
공통 옵션: `--dry-run`, `--no-validate`, `--actor <id>`, `--project <dir>`. 실패하면 `error.ruleId`(`ENTITY_NOT_FOUND`, `PROPERTY_OUT_OF_RANGE`, `VALIDATION_FAILED`, `BASE_MISMATCH` …)와 `error.details`. 전체 목록은 `Engine/Commands/README.md`.

## 데이터화 테스트 (§23, §24)

```bash
cd Game
..\build\msvc-headless\bin\akeir.exe test --list --json                 # Tests/**/*.test.json 목록
..\build\msvc-headless\bin\akeir.exe test --json                        # 전부 실행 → Tests/.results/<run>/results.json, exit 0/3
..\build\msvc-headless\bin\akeir.exe test Combat --junit Cache\junit.xml --json   # 이름/경로 필터 + JUnit
```
실패하면 `error.details.tests[].failures[]` 에 `{assertId, tick, bindings, note}` 와 `artifacts[]` 의 snapshot 경로가 있다. 시나리오 형식과 표현식 문법은 `Engine/Testing/README.md`.

## 창 모드 · capture (Phase 2, SDL 빌드 = `msvc-debug`/`msvc-release`; `msvc-headless` 만 제외)

```bash
cd Game
..\build\msvc-debug\bin\akeir.exe run --ticks 600 --json                         # 창을 열고 600 tick (ESC/닫기로 중단). WASD/화살표 = MoveX/MoveY
..\build\msvc-debug\bin\akeir.exe run --record Cache\rec.jsonl --json              # 입력을 기록 (닫을 때까지)
..\build\msvc-debug\bin\akeir.exe run --headless --replay Cache\rec.jsonl --ticks N --json   # 같은 finalHash 가 나와야 한다
..\build\msvc-debug\bin\akeir.exe capture --ticks 300 --width 512 --height 512 --out Cache\capture\t300.png --json
..\build\msvc-debug\bin\akeir.exe capture --ticks 300 --compare Cache\capture\t300.png --diff Cache\capture\diff.png --json   # §27.1 비교, exit 3 on mismatch
..\build\msvc-debug\bin\akeir.exe input map --json                                 # input.json → scancode
..\build\msvc-debug\bin\akeir.exe test Visual --update-golden --json               # 골든 생성/갱신 (Tests/Golden/<test>/)
```
`msvc-headless` 빌드에서 이 명령들은 `FEATURE_UNAVAILABLE`(exit 1) 이고, `requires: ["renderer"]` 테스트는 skipped 다.

## 상주 프로세스 · MCP (Phase 4/7)

```bash
cd Game
start ..\build\msvc-headless\bin\akeir.exe serve --idle-timeout 600000     # 다른 창에서: 첫 줄이 {port, token, pid}; Cache/serve.json 생성
..\build\msvc-headless\bin\akeir.exe serve status --json                  # 요청 수, 열린 tx, 최근 history
..\build\msvc-headless\bin\akeir.exe set name:Goblin_01 Health.max 45 --json   # 데몬으로 포워딩 (meta.via = "serve")
..\build\msvc-headless\bin\akeir.exe tx begin --json                       # → result.tx (TTL 10분)
..\build\msvc-headless\bin\akeir.exe entity create Crate --parent path:TestArena/Arena --tx tx_XXXX --json
..\build\msvc-headless\bin\akeir.exe tag add name:Crate breakable --tx tx_XXXX --json   # tx 안에서는 자기 변경이 보인다
..\build\msvc-headless\bin\akeir.exe tx commit tx_XXXX --json             # 하나의 history 항목, 파일 기록
..\build\msvc-headless\bin\akeir.exe run --headless --ticks 120 --json    # result.run = run handle
..\build\msvc-headless\bin\akeir.exe run status --json                    # 이 serve 세션의 run 들
..\build\msvc-headless\bin\akeir.exe set name:Goblin_01 Health.max 30 --local --json   # 데몬 무시 (두 writer → BASE_MISMATCH 가능)
..\build\msvc-headless\bin\akeir.exe serve stop
```

MCP 클라이언트 등록 (stdio). 예: Claude Code `.mcp.json`
```json
{ "mcpServers": { "akeir": { "command": "C:\\Project\\Project_ME\\build\\msvc-debug\\bin\\akeir.exe", "args": ["mcp", "--project", "C:\\Project\\Project_ME\\Game"] } } }
```
손으로 확인하려면 한 줄씩 stdin 으로:
```bash
echo {"jsonrpc":"2.0","id":1,"method":"tools/list"} | ..\build\msvc-headless\bin\akeir.exe mcp
```

## 의존성

버전은 `cmake/Dependencies.cmake` 한 곳에 고정되어 있다 (설계 문서 §3 표와 같아야 한다):

| 이름 | 태그 | 라이선스 | 용도 |
|---|---|---|---|
| nlohmann/json | v3.12.0 | MIT | JSON 문서, RFC 6901/6902 |
| Flecs | v4.1.6 | MIT | ECS (runtime world) |
| Box2D | v3.1.1 | MIT | 2D physics |
| doctest | v2.5.3 | MIT | 테스트 |
| SDL3 | release-3.4.14 | zlib | 창/입력/headless driver (`AKEIR_WITH_SDL=ON` 일 때만) |

소스는 `.cpm-cache/<dir>/` 에 shallow clone 되어 있으면 그것을 쓰고, 없으면 configure 때 GitHub 에서 **shallow clone** 으로 받는다 (릴리즈 zip 에는 `.cpm-cache/` 가 없다 — 첫 configure 에 네트워크가 필요하고 1분쯤 걸린다).
새 머신에서 캐시를 미리 만들려면:

```bash
scripts\fetch-deps.ps1     # 또는 아래 수동
git clone --depth 1 --branch v4.1.6         https://github.com/SanderMertens/flecs.git .cpm-cache/flecs
git clone --depth 1 --branch v3.1.1         https://github.com/erincatto/box2d.git     .cpm-cache/box2d
git clone --depth 1 --branch v3.12.0        https://github.com/nlohmann/json.git       .cpm-cache/json
git clone --depth 1 --branch v2.5.3         https://github.com/doctest/doctest.git     .cpm-cache/doctest
git clone --depth 1 --branch release-3.4.14 https://github.com/libsdl-org/SDL.git      .cpm-cache/sdl
```

## 결정론 플래그 (§22.2)

`cmake/DetFpFlags.cmake` 가 `det_fp_flags` INTERFACE target 을 만든다: MSVC `/fp:precise`, Clang/GCC `-ffp-contract=off -fno-fast-math`.
모든 엔진 타깃이 link 하고, Box2D 에는 `AKEIR_FP_FLAGS_OPTIONS` 를 직접 건다 (export set 때문).
`akeir version --json` 의 `fpFlagsHash` 가 적용된 플래그의 해시다 — replay header(§22.3)와 비교한다.

## 상주 프로세스가 떠 있는 동안의 재빌드 (ADR-0034)

`akeir serve` 나 Claude Code 의 `akeir mcp` 가 돌고 있어도 `scriptsuild.cmd … build` 는 성공한다 — 링크 직전에 `cmake/UnlockExe.cmake` 가 잠긴 `binkeir.exe` 를 `akeir.exe.stale-<stamp>` 로 옮기고 새 파일을 쓴다 (상주 프로세스는 옮겨진 파일에서 계속 돈다; stale 파일은 다음 링크 때 정리). MCP 는 다음 tool 호출부터 새 빌드의 worker 가 답하고 응답에 `MCP_WORKER_RESTARTED` note 가 붙는다. `akeir serve` 는 이전 빌드로 계속 답하며 포워딩된 envelope 에 `SERVE_STALE_EXE` 경고가 붙는다 → `akeir serve stop` 후 다시 띄운다. 어느 빌드가 돌고 있는지는 `akeir version --json` 의 `result.exe.sha256`. end-to-end 검사: `python scripts	est_resident_rebuild.py --preset msvc-headless`.

## 알려진 빌드 이슈

- **doctest + MSVC 14.44**: `<string_view>` 와 doctest 의 `std::basic_ostream` 전방 선언이 충돌 → `Tests/CMakeLists.txt` 에 `DOCTEST_CONFIG_USE_STD_HEADERS`. 제거하면 `error C2027: 정의되지 않은 형식 'std::basic_ostream'` 이 난다.
- **CPM 경고 "unstable development version"**: vendored CPM.cmake 가 git clone 본이라 버전 문자열이 비어 있다 → `Dependencies.cmake` 가 `EXTRACTED_CPM_VERSION` 을 지정해 억제.
- **batch 파일에 UTF-8 한글 주석 금지**: cmd.exe 가 코드페이지 949 로 파싱해 깨진 줄을 실행한다. `scripts/*.cmd` 는 ASCII 만.
- **빌드 디렉터리를 바꿔 configure 한 뒤 CPM 경고**: 이전 configure 의 `CPM_<name>_SOURCE` 캐시 변수가 남아 경고가 난다. `build/<preset>` 삭제 후 재구성.
