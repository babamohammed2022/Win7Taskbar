# Win7Taskbar - Core nativo (Win32 DLL)
# Copyright (c) 2026 Win7Taskbar contributors
# Licensed under the GNU General Public License
# version 3 or later (see the LICENSE file).

cmake_minimum_required(VERSION 3.20)
# Pin the Windows SDK before project(): the current runner image defaults to
# the 10.0.26100 SDK whose import libraries are broken (uuid.lib not found;
# actions/runner-images #13310), so the link step fails there. 10.0.22621 is
# the newest SDK that links cleanly on this project.
set(CMAKE_SYSTEM_VERSION "10.0.22621.0" CACHE STRING "Windows SDK version" FORCE)
project(Win7TaskbarCore VERSION 1.7.0 DESCRIPTION "Win7Taskbar native backend" LANGUAGES CXX RC)

# Temporary full-project probe: configure+build the whole native project in
# a subdirectory and re-emit the lines around its first toolchain error as
# ": error C999:" / ": warning C999:" text during the outer build phase (the
# only phase the runner's problem matcher annotates; raw job logs are not
# reachable from automation). The guard keeps the inner build from
# recursing into the probe.
if(NOT W7T_INNER)
    set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
    try_compile(W7T_FULL_RESULT "${CMAKE_BINARY_DIR}/full-probe"
        "${CMAKE_SOURCE_DIR}" "CMakeLists.txt"
        CMAKE_FLAGS -DW7T_INNER:BOOL=ON
        OUTPUT_VARIABLE W7T_FULL_OUTPUT)
    set(W7T_ALL "")
    string(REPLACE ";" "\\;" W7T_PROBE_ESCAPED "${W7T_FULL_OUTPUT}")
    string(REPLACE "\n" ";" W7T_PROBE_LINES_KEEP "${W7T_PROBE_ESCAPED}")
    string(REPLACE "\n\n" "\n \n" W7T_PROBE_LINES_KEEP "${W7T_PROBE_LINES_KEEP}")
    foreach(W7T_LINE ${W7T_PROBE_LINES_KEEP})
        list(APPEND W7T_ALL "${W7T_LINE}")
    endforeach()
    set(W7T_HIT -1)
    set(W7T_IDX 0)
    foreach(W7T_LINE ${W7T_ALL})
        if(W7T_HIT EQUAL -1 AND W7T_LINE MATCHES "MSB[0-9]|LNK[0-9]|fatal error|MANCANTE")
            set(W7T_HIT ${W7T_IDX})
        endif()
        math(EXPR W7T_IDX "${W7T_IDX} + 1")
    endforeach()
    set(W7T_DIAG_TEXT "")
    list(LENGTH W7T_ALL W7T_N)
    math(EXPR W7T_LAST "${W7T_N} - 1")
    if(W7T_HIT GREATER -1)
        math(EXPR W7T_START "${W7T_HIT} - 12")
        if(W7T_START LESS 0)
            set(W7T_START 0)
        endif()
        math(EXPR W7T_END "${W7T_HIT} + 3")
        if(W7T_END GREATER ${W7T_LAST})
            set(W7T_END ${W7T_LAST})
        endif()
        foreach(W7T_I RANGE ${W7T_START} ${W7T_END})
            list(GET W7T_ALL ${W7T_I} W7T_L)
            string(APPEND W7T_DIAG_TEXT "${W7T_L}\n")
        endforeach()
    elseif(W7T_N GREATER 0)
        if(W7T_LAST GREATER 15)
            set(W7T_LAST 15)
        endif()
        foreach(W7T_I RANGE 0 ${W7T_LAST})
            list(GET W7T_ALL ${W7T_I} W7T_L)
            string(APPEND W7T_DIAG_TEXT "${W7T_L}\n")
        endforeach()
    endif()
    if(W7T_FULL_RESULT)
        string(PREPEND W7T_DIAG_TEXT "FULL PROBE OK\n")
    else()
        string(PREPEND W7T_DIAG_TEXT "FULL PROBE FAILED\n")
    endif()
    file(WRITE "${CMAKE_BINARY_DIR}/full-probe-diag.txt" "${W7T_DIAG_TEXT}")
    file(WRITE "${CMAKE_BINARY_DIR}/echo-probe.cmake"
"file(STRINGS \"${CMAKE_BINARY_DIR}/full-probe-diag.txt\" lines)\n"
"set(i 0)\n"
"foreach(l IN LISTS lines)\n"
"    if(i LESS 9)\n"
"        message(\"native/src/probe.cmake(1): error C999: P\${i}: \${l}\")\n"
"    else()\n"
"        message(\"native/src/probe.cmake(1): warning C999: P\${i}: \${l}\")\n"
"    endif()\n"
"    math(EXPR i \"\${i} + 1\")\n"
"endforeach()\n")
    add_custom_target(w7tProbeDiag ALL
        COMMAND ${CMAKE_COMMAND} -P "${CMAKE_BINARY_DIR}/echo-probe.cmake"
        COMMENT "Echo full-project probe result" VERBATIM)
endif()
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT WIN32)
    message(FATAL_ERROR
        "Win7TaskbarCore can only be built for Windows. On Linux use the MinGW-w64 "
        "toolchain: -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake")
endif()
add_library(Win7TaskbarCore SHARED
    src/Common.cpp src/WindowManager.cpp src/TrayService.cpp src/TrayToolbar.cpp
    src/SystemEventsWatch.cpp src/AppBarService.cpp src/ShellMenu.cpp
    src/ImmersiveFlyouts.cpp src/FlyoutLauncher.cpp src/AudioService.cpp
    src/ExplorerTrayReader.cpp src/Win11TrayReader.cpp
    src/TrayOverflowWindow.cpp src/AppSearchWindow.cpp
    src/Win7NetworkFlyout.cpp src/BatteryFlyout.cpp src/JumpListWindow.cpp
    src/PropertiesDialog.cpp src/PinnedApps.cpp src/Exports.cpp
    src/LanguageSwitcher.cpp
    src/Strings.cpp src/LocalizationExports.cpp
    src/TrayFallbackIcons.cpp resources/app.rc)
target_include_directories(Win7TaskbarCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(Win7TaskbarCore PRIVATE W7T_BUILDING_DLL=1 UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(Win7TaskbarCore PRIVATE user32 gdi32 shell32 ole32 oleaut32 comctl32 wtsapi32 dwmapi uxtheme msimg32 propsys shlwapi advapi32 powrprof wlanapi iphlpapi uuid psapi windowscodecs)
set_source_files_properties(resources/app.ico PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(Win7TaskbarCore PRIVATE resources/app.ico)
set_target_properties(Win7TaskbarCore PROPERTIES PREFIX "" OUTPUT_NAME "Win7TaskbarCore")
if(MSVC)
    target_compile_options(Win7TaskbarCore PRIVATE /W4 /permissive- /utf-8)
    # The runner-image roll-out of 2026-09-13 (newer MSVC/linker) can strip
    # the COM GUID blobs and export surface this project's POST-BUILD checks
    # require, failing the build silently. Keep linker dead-stripping and
    # COMDAT folding off for the core DLL so the binary keeps them.
    target_link_options(Win7TaskbarCore PRIVATE "/OPT:NOREF" "/OPT:NOICF")
else()
    target_compile_options(Win7TaskbarCore PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type)
    target_link_options(Win7TaskbarCore PRIVATE -static-libgcc -static-libstdc++ -static)
endif()
set(W7T_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../dist" CACHE PATH "Folder for the compiled DLL")
# Stage markers (temporary diagnostics): plain echos shaped like compiler
# errors so the runner annotates which build stages actually ran.
add_custom_command(TARGET Win7TaskbarCore PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "native/src/CMakeLists.txt(1): error C999: MARKER pre-build reached"
    COMMENT "Marker: pre-build" VERBATIM)
add_custom_command(TARGET Win7TaskbarCore POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${W7T_DIST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Win7TaskbarCore> "${W7T_DIST_DIR}/"
    COMMENT "Copy Win7TaskbarCore.dll to dist/")
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    # The checks run through run-check.py: same exit code, but on failure it
    # also prints a "file(1): error C999:" line that the runner's C++
    # problem matcher turns into a check annotation naming the failed check.
    add_custom_command(TARGET Win7TaskbarCore POST_BUILD
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/run-check.py" check-com-interfaces.py $<TARGET_FILE:Win7TaskbarCore>
        COMMENT "Check COM interfaces in binary" VERBATIM)
    # v2.62: la tabella degli export deve contenere TUTTE le funzioni che il
    # livello gestito richiama. Una build che ne perde qualcuna compila, si
    # avvia e fallisce al primo clic con EntryPointNotFoundException: e'
    # successo davvero, con nove funzioni mancanti in dist/.
    add_custom_command(TARGET Win7TaskbarCore POST_BUILD
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/run-check.py" check-exports.py $<TARGET_FILE:Win7TaskbarCore>
        COMMENT "Check exported functions in binary" VERBATIM)
endif()
add_library(W7TInject SHARED src/InjectHook.cpp)
set_target_properties(W7TInject PROPERTIES PREFIX "" OUTPUT_NAME "W7TInject")
target_link_libraries(W7TInject PRIVATE user32 comctl32)
if(NOT MSVC)
    target_link_options(W7TInject PRIVATE -static-libgcc -static-libstdc++ -static)
endif()
add_custom_command(TARGET W7TInject POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${W7T_DIST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:W7TInject> "${W7T_DIST_DIR}/"
    COMMENT "Copy W7TInject.dll to dist/")
install(TARGETS Win7TaskbarCore W7TInject RUNTIME DESTINATION bin)
install(FILES include/Win7TaskbarCore.h DESTINATION include)
