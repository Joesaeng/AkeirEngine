# cmake/UnlockExe.cmake — run as a PRE_LINK step of the `akeir` target on Windows (ADR-0034).
#
#   cmake -DEXE=<path/to/akeir.exe> -P cmake/UnlockExe.cmake
#
# Windows keeps the image file of a running process locked against overwrite/delete, so relinking
# akeir.exe while `akeir mcp` (Claude Code keeps it resident) or `akeir serve` runs fails with LNK1168.
# The file may still be *renamed*: we move the in-use file aside as `akeir.exe.stale-<stamp>` and let the
# linker write a fresh akeir.exe. The resident process keeps running from the renamed file; the MCP
# adapter notices the new akeir.exe and restarts its worker from it (Tools/CLI/src/Mcp.cpp).
# Stale copies are deleted on later links once their process has exited (REMOVE silently skips locked ones).
#
# Probe: `file(APPEND)` of nothing opens the file for writing without modifying it — it fails only when the
# file is in use. (Do NOT use file(LOCK): it truncates the file.)

if(NOT EXE)
  message(FATAL_ERROR "UnlockExe.cmake: -DEXE=<path> is required")
endif()

if(EXISTS "${EXE}")
  set(_probe "${CMAKE_CURRENT_LIST_DIR}/UnlockExeProbe.cmake")
  execute_process(COMMAND "${CMAKE_COMMAND}" -DEXE=${EXE} -P "${_probe}"
                  RESULT_VARIABLE _locked OUTPUT_QUIET ERROR_QUIET)
  if(NOT _locked EQUAL 0)
    string(TIMESTAMP _stamp "%Y%m%d-%H%M%S")
    string(RANDOM LENGTH 4 ALPHABET 0123456789abcdef _rand)
    set(_stale "${EXE}.stale-${_stamp}-${_rand}")
    file(RENAME "${EXE}" "${_stale}")
    if(EXISTS "${EXE}")
      message(FATAL_ERROR "UnlockExe.cmake: ${EXE} is in use and could not be moved aside — stop `akeir serve` / the MCP client and rebuild")
    endif()
    message(STATUS "akeir.exe is in use (akeir serve / akeir mcp running) — moved aside as ${_stale}; the resident process keeps using it")
  endif()
endif()

# sweep stale copies whose process has exited (locked ones survive REMOVE silently)
file(GLOB _stales "${EXE}.stale-*")
foreach(_f IN LISTS _stales)
  file(REMOVE "${_f}")
endforeach()
