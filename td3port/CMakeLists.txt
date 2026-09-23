cmake_minimum_required(VERSION 3.24)
project(td3port C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Port of TDIII.EXE, VGA (mode 13h) with AdLib sound. EGA / Tandy / MT-32 / Tandy / CMS are parked
# (PLAN.md, TODO).

find_package(SDL3 REQUIRED CONFIG)

# Nuked-OPL3 (LGPL-2.1), built as its own library and linked in; see third_party/nuked-opl3/README.md.
add_library(nuked_opl3 STATIC third_party/nuked-opl3/opl3.c)
target_include_directories(nuked_opl3 PUBLIC third_party/nuked-opl3)

file(GLOB_RECURSE TD_SOURCES CONFIGURE_DEPENDS src/*.c)

add_executable(td3port ${TD_SOURCES})
target_include_directories(td3port PRIVATE src)
target_link_libraries(td3port PRIVATE SDL3::SDL3 nuked_opl3)
if(MSVC)
    target_compile_options(td3port PRIVATE /W3)
else()
    # The memory model reads multi-byte values through byte pointers (see src/mem.h).
    target_compile_options(td3port PRIVATE -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing)
    # Game strings are copied out of mem[] into fixed buffers with snprintf; truncation is intended.
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_compile_options(td3port PRIVATE -Wno-format-truncation)
    endif()
endif()
if(WIN32)
    add_custom_command(TARGET td3port POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:td3port> $<TARGET_FILE_DIR:td3port>
        COMMAND_EXPAND_LISTS)
endif()

# The launcher ("Test Drive III"): the game folder, the car, course and skill the game starts with, the
# game speed, sound and display options; built beside td3port.exe. It needs wxWidgets 3.2
# (MSYS2: mingw-w64-x86_64-wxwidgets3.2-msw) and a C++17 compiler.
option(TD3_LAUNCHER "Also build the launcher, Test Drive III.exe (wxWidgets)" OFF)
if(TD3_LAUNCHER)
    add_subdirectory(launcher)
endif()
