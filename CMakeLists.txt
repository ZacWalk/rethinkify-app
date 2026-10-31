cmake_minimum_required(VERSION 3.21)

project(dde LANGUAGES CXX VERSION 1.0.0)

if(NOT MSVC)
    message(FATAL_ERROR "dde targets the Windows SDK through MSVC; no other toolchain is supported.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Statically linked CRT, so the executable runs with no redistributable. Set
# before the platform layer is pulled in, so it is built the same way.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_CXX_FLAGS_DEBUG "/Od /Zi /RTC1 /D_DEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "/Ox /Oi /Ot /Oy /GF /Gy /Zi /DNDEBUG")

# Develop against a sibling libdesktop checkout when one exists; CI and a fresh
# clone fall back to the pinned revision below.
if(NOT DEFINED FETCHCONTENT_SOURCE_DIR_LIBDESKTOP
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../libdesktop/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_LIBDESKTOP "${CMAKE_CURRENT_SOURCE_DIR}/../libdesktop"
        CACHE PATH "Local libdesktop checkout to build against")
endif()

include(FetchContent)
FetchContent_Declare(libdesktop
    GIT_REPOSITORY https://github.com/ZacWalk/libdesktop.git
    GIT_TAG        3d3b61378fc79444ba297abbb0b860793c72b2d3
)
FetchContent_MakeAvailable(libdesktop)

platform_add_app(dde UI
    SOURCES
        src/app.cpp
        src/app_commands.cpp
        src/app_views.cpp
        src/commands.cpp
        src/cpp_index.cpp
        src/cpp_lex.cpp
        src/dd_project.cpp
        src/document.cpp
        src/document_syntax.cpp
        src/json.cpp
        src/tests.cpp
        src/tests_cpp.cpp
        src/tool_output.cpp
        src/tool_runner.cpp
        src/util.cpp
    ICON        src/res/app.ico
    DESCRIPTION "dde, the editor for dd"
    OUTPUT_NAME dde-64
)

target_include_directories(dde PRIVATE src)
target_compile_options(dde PRIVATE /W3 /MP)

target_link_options(dde PRIVATE
    /DEBUG
    $<$<CONFIG:Release>:/OPT:REF>
    $<$<CONFIG:Release>:/OPT:ICF>
)

set_target_properties(dde PROPERTIES
    DEBUG_POSTFIX d
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/exe"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/exe"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/exe"
)

target_precompile_headers(dde PRIVATE src/pch.h)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME dde.unit COMMAND dde /test)
    set_tests_properties(dde.unit PROPERTIES
        LABELS "dde;unit"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        TIMEOUT 120
    )
endif()
