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

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Self-sufficiency: link the CRT statically in every configuration. A DLL
# built with the CMake default (/MD) silently depends on vcruntime140.dll,
# vcruntime140_1.dll and msvcp140.dll, which a clean Windows 10/11 machine
# does not ship: LoadLibraryW then fails and the app reports the native core
# as "not found" even though the file sits right next to the executable.
# The core owns its CRT state (flat C exports, every buffer owned by the
# caller), so not sharing the CRT with the host process is safe here. The
# MinGW branch below already links -static for the same reason.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT WIN32)
    message(FATAL_ERROR
        "Win7TaskbarCore can only be built for Windows. On Linux use the MinGW-w64 "
        "toolchain: -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake")
endif()
add_library(Win7TaskbarCore SHARED
    src/Common.cpp src/WindowManager.cpp src/TrayService.cpp src/TrayToolbar.cpp
    src/SystemEventsWatch.cpp src/AppBarService.cpp src/ShellMenu.cpp
    src/ImmersiveFlyouts.cpp src/FlyoutLauncher.cpp src/AudioService.cpp
    src/ExplorerTrayReader.cpp src/Win11TrayReader.cpp src/Win11TrayReaderResilience.cpp
    src/TrayOverflowWindow.cpp src/AppSearchWindow.cpp
    src/Win7NetworkFlyout.cpp src/Win8NetworkFlyout.cpp src/BatteryFlyout.cpp src/JumpListWindow.cpp
    src/PropertiesDialog.cpp src/PinnedApps.cpp src/Exports.cpp
    src/ExtraSettings.cpp
    src/LanguageSwitcher.cpp src/AeroThumbnailFrame.cpp src/PreviewPolicy.cpp src/TaskbarButtonNotify.cpp src/PinVerbs.cpp
    src/Strings.cpp src/LocalizationExports.cpp
    src/TrayFallbackIcons.cpp src/CrashHandler.cpp
    src/TrayPrefsStore.cpp
    src/DpiMetrics.cpp src/WindowSubclass.cpp
    src/AppBarPositioner.cpp src/TaskbarWindow.cpp src/BandLayout.cpp
    src/TraySizer.cpp
    src/TrayIconHost.cpp src/ShellHookReceiver.cpp
    resources/app.rc)
target_include_directories(Win7TaskbarCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# v1.21.18: the revision the DLL is built from, compiled into the binary and
# written to log-core.txt at the first log line ("=== Win7Taskbar core build:
# <stamp> ==="). The release workflow passes the commit SHA and then checks
# that the SAME string is inside the packaged DLL, so a package carrying a
# stale native core cannot leave CI unnoticed. Unset = local build.
set(W7T_BUILD_STAMP "local" CACHE STRING "Revision stamped into the native core")
# No -DW7T_BUILD_STAMP (which would need a change to the release workflow, so it
# is only a convenience)? Take the revision from the checkout itself: the
# release build then stamps the commit it is built from with no workflow
# change at all.
if(W7T_BUILD_STAMP STREQUAL "local")
    find_package(Git QUIET)
    if(Git_FOUND)
        execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
                        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                        OUTPUT_VARIABLE W7T_GIT_SHA
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET)
        if(W7T_GIT_SHA)
            set(W7T_BUILD_STAMP "${W7T_GIT_SHA}")
        endif()
    endif()
endif()
message(STATUS "Win7TaskbarCore build stamp: ${W7T_BUILD_STAMP}")
target_compile_definitions(Win7TaskbarCore PRIVATE W7T_BUILDING_DLL=1 UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00 "W7T_BUILD_STAMP=L\"${W7T_BUILD_STAMP}\"")
# comdlg32: v1.21.7, ChooseColorW for the colour picker of the extra
# settings tab (the standard Windows colour chooser).
target_link_libraries(Win7TaskbarCore PRIVATE user32 gdi32 shell32 ole32 oleaut32 comctl32 wtsapi32 dwmapi uxtheme msimg32 propsys shlwapi advapi32 powrprof wlanapi iphlpapi uuid psapi windowscodecs comdlg32)
set_source_files_properties(resources/app.ico PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(Win7TaskbarCore PRIVATE resources/app.ico)
set_target_properties(Win7TaskbarCore PROPERTIES PREFIX "" OUTPUT_NAME "Win7TaskbarCore")
if(MSVC)
    target_compile_options(Win7TaskbarCore PRIVATE /W4 /permissive- /utf-8
        # Accepted diagnostics: C4611 setjmp/C++-RAII mix is intentional
        # inside the SEH guards; the others are known pre-existing warnings.
        /wd4611 /wd4189 /wd4100 /wd4244 /wd4457)
else()
    target_compile_options(Win7TaskbarCore PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type)
    target_link_options(Win7TaskbarCore PRIVATE -static-libgcc -static-libstdc++ -static)
endif()
set(W7T_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../dist" CACHE PATH "Folder for the compiled DLL")
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
    # v1.1.1: the 9-slice constants in src/AeroThumbnailFrame.h, the slice PNGs
    # in Resources/ and the bands of the XAML frame template describe ONE
    # border, and the live thumbnail has to fill its aperture at every display
    # scaling. A frame bitmap rendered in device pixels instead of the layout's
    # own unit left an empty strip inside the border on a 125% display, which
    # no compiler can catch: check-preview-geometry.py reads the four sources
    # and the numbers behind them.
    add_custom_command(TARGET Win7TaskbarCore POST_BUILD
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/run-check.py" check-preview-geometry.py $<TARGET_FILE:Win7TaskbarCore>
        COMMENT "Check preview frame geometry against the frontend" VERBATIM)
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
