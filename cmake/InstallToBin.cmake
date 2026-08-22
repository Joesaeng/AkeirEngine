# cmake/InstallToBin.cmake — POST_BUILD of the `akeir` target (ADR-0034).
#
#   cmake -DEXE=<built akeir.exe> -DDEST=<repo>/bin/akeir.exe -P cmake/InstallToBin.cmake
#
# The release zip ships a prebuilt `bin/akeir.exe` and its `.mcp.json` points at it. After someone rebuilds
# from source (new Game/Source code), that file must follow, otherwise the MCP server keeps running the
# prebuilt binary forever. When `<repo>/bin/akeir.exe` exists (zip layout) the fresh build is copied over it —
# moving a locked (resident) file aside first, exactly like the link step does — and a running `akeir mcp`
# picks the new file up on its next request. In a plain git checkout there is no `bin/akeir.exe`, so nothing happens.

if(NOT EXE OR NOT DEST)
  message(FATAL_ERROR "InstallToBin.cmake: -DEXE=<built exe> -DDEST=<install path> are required")
endif()
if(NOT EXISTS "${DEST}")
  return()
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -DEXE=${DEST} -P "${CMAKE_CURRENT_LIST_DIR}/UnlockExe.cmake" RESULT_VARIABLE _r)
if(NOT _r EQUAL 0)
  message(WARNING "InstallToBin.cmake: could not move the in-use ${DEST} aside; bin/akeir.exe was NOT updated")
  return()
endif()
file(COPY_FILE "${EXE}" "${DEST}" RESULT _copy)
if(_copy)
  message(WARNING "InstallToBin.cmake: copy failed: ${_copy}")
else()
  message(STATUS "akeir: updated ${DEST} from the new build")
endif()
