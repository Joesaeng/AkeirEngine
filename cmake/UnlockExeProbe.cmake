# cmake/UnlockExeProbe.cmake — helper of UnlockExe.cmake. Exits non-zero when ${EXE} cannot be opened
# for writing (= a process is running from it). Appending nothing leaves the file unchanged.
file(APPEND "${EXE}" "")
