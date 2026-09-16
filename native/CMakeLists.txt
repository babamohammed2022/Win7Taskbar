# Win7Taskbar - Core nativo (Win32 DLL)
# Copyright (c) 2026 Win7Taskbar contributors
# Licensed under the GNU General Public License
# version 3 or later (see the LICENSE file).

cmake_minimum_required(VERSION 3.20)
project(Win7TaskbarCore VERSION 1.7.0 DESCRIPTION "Win7Taskbar native backend" LANGUAGES CXX RC)
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
    add_custom_command(TARGET Win7TaskbarCore POST_BUILD
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-com-interfaces.py" $<TARGET_FILE:Win7TaskbarCore>
        COMMENT "Check COM interfaces in binary" VERBATIM)
    # v2.62: la tabella degli export deve contenere TUTTE le funzioni che il
    # livello gestito richiama. Una build che ne perde qualcuna compila, si
    # avvia e fallisce al primo clic con EntryPointNotFoundException: e'
    # successo davvero, con nove funzioni mancanti in dist/.
    add_custom_command(TARGET Win7TaskbarCore POST_BUILD
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-exports.py" $<TARGET_FILE:Win7TaskbarCore>
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
